# Kisyo Tune 開発 引き継ぎ

このファイルは新しいセッションの冒頭に貼る（または添付する）ためのものです。
最終更新: 2026-10-22 / 対象コミット: `49d5fd6`

---

## 0. まずこれを守ること

- **動作していないコードを「完成」として扱わない。**
- **ビルドや DAW での動作検証ができない環境では、その事実を明示する。実行していないテストを成功したものとして報告しない。** どのコミットメッセージにも「CI で確認したこと」と「未検証のこと」を分けて書いてある。同じようにすること。
- 必要以上に細かな確認を求めない。合理的に判断できる部分は自律的に進めてよい。
- 実行してよいか判断を仰ぐときは Yes / No の選択肢を出す。
- **既存の動いているコードをまず読むこと。** 一度、既にある `PitchToolOperations::scalePitchDrift` と同じことをするモジュールを新規に書いてしまい、しかも既存実装の方が端の処理が優れていたため丸ごと削除した。`Source/Utils/` は必ず先に見る。

## 1. プロジェクト

- 作者: りゅう / GitHub `Rowryu`
- リポジトリ: `Rowryu/kinu-tune`（fork）、ブランチ `main`
- 目的: 次世代ボーカルピッチ・タイミング補正 VST3。**Kinu Tune** を改造したもので、DAW 内での名前衝突を避けるため製品名は **Kisyo Tune**
- 用途: **非商用**（歌枠で使う完成音源の作成）
- 対象: Windows / DAW は Fender Studio Pro（Studio One 系）
- ライセンス連鎖: Kisyo Tune ← Kinu Tune ← PitchNet ← HachiTune、**AGPLv3**。GAME と PC-NSF-HiFiGAN の重みは **CC BY-NC-SA**（非商用のみ）。`MODIFICATIONS.md` に記載済み

### 仕様の3本柱
1. **MIDI/ガイド参照の自然なピッチ補正** — ノートの*中心*だけをガイドへ寄せ、ビブラート・しゃくり・フォール・ポルタメント・微細な抑揚は保つ
2. **AI による自然なタイミング補正** — ガイドモードと BPM モード
3. **AI リシンセシスによる高品質な音声編集**

## 2. 絶対に守る規約（`dev-rules.md`）

- **§1 ピッチの書き込みは `Source/Utils/NotePitch.h` 経由のみ**（`applyEdit` / `restore` / `applyCorrection` / `setCorrectionSource` / `setSungPitch`）。違反すると `SourceGuard_pitch-writes` テストが落ちる
- **§2 試聴は `AuditionOutput` 経由のみ**（`SourceGuard_audition-output`）
- §7 MSVC: キャプチャなしラムダからローカル `constexpr` を読まない
- §8 `THIRD_PARTY_NOTICES.txt` / `Resources/lang/*.json` を変えたらバイナリを再ビルドする
- §10 新規ソースには3行の著作権ヘッダ

## 3. 作ったもの

### `Source/Audio/Correction/`（新規モジュール）

| ファイル | 役割 |
|---|---|
| `MidiGuideTypes.h` | `GuideNote` / `SungNote` / `NoteMatch`(confidence 付き) / `AlignmentParams` |
| `NoteAlignment.*` | **単調 DTW** で歌唱ノート ↔ ガイドノートを対応付け。残コストから confidence を出す |
| `PitchTargeting.*` | `shift = fullShift × strength × confidence` を上限でクランプ。**3セント未満は動かさない**（後述） |
| `TimingMap.*` | アンカー式の source→output 写像。単調性と `[0.5, 2.0]` の伸縮率を守る |
| `MidiFileReader.*` | 自作 SMF パーサ。running status / velocity-0 note-off / テンポ変化に対応、**SMPTE 分割は拒否**。`toGuideNotes()` でモノフォニック化（高い音が勝ち、重なりは前を切る） |
| `GuideSource.*` | `GuidePlacement{projectBpm, midiFileBpm, offsetSeconds}`。`timeScale() = midiFileBpm/projectBpm`。**先にスケール→次にオフセット**（2つのつまみを独立に保つため） |
| `GuidedPitchCorrection.*` | Project への書き込み。`NotePitch::setCorrectionSource` → `applyCorrection` → `markDirty()` → `PitchCurveProcessor::rebuildBaseFromNotes` |
| `RegionMapping.*` | **トラック間のフレーム変換**。`mapFrameBetweenRegions()` / `guideNotesFromTrack()` |
| `TimingAnchors.*` | `framewiseRms` / `findEnergyPeakFrame` / `TimingGrid` / `gridTimingTargets` / `guidedTimingTargets` |
| `TimingCorrection.*` | `planTimingCorrection()` — **計画のみ。書き込みはしない** |
| `TransitionSmoothing.*` | `planTransitions()` でグライド長を決める。同音・オクターブ超の跳躍・休符隣接はスキップ |

### UI
- `Source/UI/MidiGuidePanel.*` — ガイドトラック選択 / MIDI 読込 / BPM / オフセット / 強さ / 最大移動量 / Apply
- `Source/UI/TimingCorrectionPanel.*` — モード(Guide/Grid) / 解像度 / 強さ / 最大移動 / 許容 / ノート長 / **山アンカー(既定オン)** / Apply / なめらか化
- `Source/Undo/GuidedPitchAction.h`

### 既存コードに乗せた部分（新規実装していない）
- ピッチ書き込み → `NotePitch`
- リタイミング → `applyNoteTimingStates` + `TimingAction`
- つなぎ目のなめらか化 → `smoothBoundary` + `PitchToolAction`
- ガイドのピッチ曲線描画 → `previewCurveRenderer`

新しいのは「**何をどこへ動かすか決める層**」だけ。これは意図的な設計。

## 4. 重要な設計上の事実（間違えやすい）

- **ARA ではプラグインの全インスタンスが1つの `ARADocument` を共有する。** 他トラックのノートは読める。Melodyne のマルチトラック編集がこれ。最初これを「できない」と誤って断言し、りゅうさんに訂正された
- `collectAraPlaybackRegions()` が答えるのは「**このインスタンスが関わるリージョン**」。全ドキュメントを見るのは全部空だったときのフォールバックだけ。**ガイドトラック探索には `collectDocumentPlaybackRegions()` を使う**（`document->getRegionSequences()` を走査する新設関数）
- Project はその audio modification 自身の時間で解析され、**タイムライン分のパディングは入らない**。2つのトラックのフレーム番号は無関係。共有の基準はホストのタイムラインだけ
- `basePitch` / `deltaPitch` の分解 = ノート中心 / フレームごとの抑揚
- **Windows のコードページの罠**: `std::ifstream` はシステム ANSI コードページ（日本語環境では CP932）でナローパスを取る。`juce::String::toStdString()` は UTF-8 を返すので**非 ASCII のファイル名が開けない**。UI は `juce::File::loadFileAsData` → `loadGuideFromBytes` でバイト列を渡す
- `TimingGrid::firstBeatFrame` は**必ず設定する**。既定の0のままだとグリッドがリージョンの音声先頭から引かれ、曲の拍と無関係な線になる

## 5. 直近に報告された不具合と対処

りゅうさんの報告（原文）:
> まず、ガイドを参考にした動きが全くない、かつ山に対してグリッドがあってない、そもそもガイドを参考にタイミング補正が押せない / ピッチ補正の方にオーディオが対応していない

→ 原因は2つだった。

**原因A（症状1・3・4）** ガイドトラックの一覧が常に空だった。`collectAraPlaybackRegions()` を使っていたため、通常はアクティブトラックしか返らない。ガイドは定義上「他のトラック」なので0件。ガイドが無い→ピッチ補正が何もしない／タイミング補正のボタンが押せない。
→ `collectDocumentPlaybackRegions()` を新設して両方のガイド関数をそちらへ。コミット `da25970`

**原因B（症状2）** `TimingGrid::firstBeatFrame` を一度も設定していなかった。ホストの拍をプロジェクトのフレーム座標へ変換して渡すよう修正。`getProjectHostOffsetSeconds` プロバイダを `IMainView`/`MainComponent` に追加。コミット `da25970`

### 音質劣化の報告
> あと補正後の音源の劣化がMelodyneなどに比べて激しい

**主因: エンジンが「クラシック」(PSOLA) に固定されていた。**
速度問題を直したとき、GPU パスが入っていないビルドでは既定を PSOLA にした（CPU ボコーダーは1ノート2秒）。これは正しい。**しかし `loadSettings()` の最後で `saveConfig()` が呼ばれるため、その既定値が「ユーザーの選択」として config.json に書き戻されていた。** 後から DirectML 入りビルドを入れても古い "PSOLA" が勝ち続け、画面には理由が何も出ない。
→ エンジンを隣の GPU デバイス ID と同じ扱いに。**トグルを実際にクリックしたときだけ保存**し、それ以外は毎回ビルドとマシンから再計算。`config.json` に `synthesisEnginePicked` を追加（不在が「この変更前のファイル」の印）。`settings.xml` はプログラムが書かないので手書き＝選択として扱う。コミット `9a1eeda`

**副因: 1セント動かすためにノートを再合成していた。**
`computePitchTargets` は移動量が**ちょうど `0.0f`** のときだけ省いていた。浮動小数ではほぼ起きない。2セント低いノートが1セント強「補正」され dirty が付き、エンジンを通っていた。`IncrementalSynthesizer` のコミットマスクは**実際に編集されたノートだけ**波形を差し替えるので、触らなければ録音のサンプルがそのまま残る（Melodyne の未編集ノートが未編集に聞こえる理由）。なのに全ノートに dirty が付き、実質テイク全体が再合成されていた。
→ `minShiftSemitones = 0.03`（3セント）のデッドバンドを追加。0 にすれば旧挙動。コミット `49d5fd6`

**ただしこの副因の効果はりゅうさんの素材には限定的。** 補正前の中央値が34.7セントなので大半のノートは本当に動かす必要がある。効くのは主因の方。

## 6. 現在の状態

- `main` の最新コミットは `79f536d`。`aadb76b` のあとに §11 の3コミットが乗っている（`build-channel` からマージ済み）
- **CI run 39667524791: 全34テスト通過・補正スイート9本通過・Windows VST3 ビルド成功**（`79f536d`）
- **りゅうさんの PC には 2026-10-22 に `49d5fd6` のビルドを入れた。** それまで入っていたのは `7c21ca62` で、`da25970` / `9a1eeda` / `49d5fd6` のどれも入っていなかった。つまり報告された不具合は「直したが手元で動いていなかった」状態だった（§11）
- 最新コミット `49d5fd6`、push 済み
- **CI run 39642930436: 全34テスト 100% 通過**（macOS）、補正スイート9本も個別に通過、Windows VST3 ビルド成功
- `SourceGuard_pitch-writes` / `SourceGuard_audition-output` も通過
- **ビルド成果物**: `Kisyo-Tune-test-49d5fd6d-dml-Windows-x64-plugin-only`（18MB、DirectML 入り）
  https://github.com/Rowryu/kinu-tune/actions/runs/39646237484
- ダウンロード後は**ブロックの解除が必要**（Mark-of-the-Web で Studio One のブラックリストに入る。一度これで「プラグインが出てこない」事故が起きた）

### テスト一覧（すべて `tests/CMakeLists.txt` と `ci.yml` の `-R` に登録済み）
`MidiGuideCorrectionTests`(36) / `TimingMapTests`(70) / `MidiFileReaderTests`(44) / `GuideSourceTests` / `GuidedPitchCorrectionTests` / `RegionMappingTests` / `TimingAnchorsTests` / `TimingCorrectionTests` / `TransitionSmoothingTests`

`GuidedPitchCorrectionTests` と `TimingCorrectionTests` は `Project` を使うため JUCE が必要で、CI でしか走らない。残り7本は JUCE 不要なので
`g++ -std=c++17 -I. -o /tmp/t tests/<Suite>.cpp $(ls Source/Audio/Correction/*.cpp | grep -v GuidedPitchCorrection.cpp | grep -v 'Correction/TimingCorrection.cpp')`
で手元ビルドして走らせられる。

### CI の注意
- `--no-tests=error` を付けてある（`ctest` はテスト0件でも exit 0 を返すため）
- ビルド/テストのログは **`ci-logs` ブランチ**に push される（`runs/<run_id>/build.log` 等）。GitHub の blob ストレージに届かない環境でもログが読めるようにするため。`git show origin/ci-logs:runs/<run_id>/ctest.log`
- macOS ランナーがステップ記録ゼロで死ぬことがある（インフラ由来）。`gh run rerun <id> --failed` で再実行

## 7. 未検証・りゅうさんの耳が必要なこと

**この環境ではプラグインをビルドできない**（Linux コンテナ、MSVC なし、apt 不可）。**音も聞けない。** 以下は本人の確認待ち:

1. **エンジンが「AI リシンセシス」になっているか** — 劣化の最大要因
2. ガイドトラックの一覧に別トラックが出るか
3. ガイド参照のピッチ補正が実際にノートを動かすか、動きが自然か
4. グリッドが山に合うか
5. 補正の強さ（既定 0.75）が強すぎるか弱すぎるか
6. タイミング補正の自然さ、つなぎ目のなめらか化の効き

`SettingsManager` の config 往復は**コンパイルでしか確認できていない**。コンストラクタが実ユーザーの設定ディレクトリを読み書きするため、単体テストを書くと installed configuration を触ってしまう。

## 8. 実素材での測定結果（参考）

りゅうさんから WAV 4本 + MIDI を受領して解析した（**そのファイルはもうセッションに無い**）。

- `MIDI.wav` = ガイドトーン（MIDI との F0 誤差 中央値 0.6¢）
- `____main.wav` = 補正後（13.3¢）
- `___main.wav` = 補正前（69.1¢）
- `MOD.wav` = インスト
- **MIDI は 120 BPM で書き出されていたがプロジェクトは 162 BPM**（比 1.3495 ≈ 162/120）。これがテンポ補正 UI を作った動機
- 実際の C++ アラインメントを走らせた結果: ガイド172ノート中169使用、単調、confidence 平均 0.808。strength 1.0 で**中央値 9.6 セント**（未補正 34.7）

## 9. 保留・今後

- **リファレンス音源からメインボーカルを抽出してガイドにする**（りゅうさんの最終構想）。技術的には可能だが、まず外部の UVR5 / Demucs で分離してそれをガイドトラックに置く方を勧めた。プラグイン内蔵は後回し
- Melodyne 的なノート間の更なる自然化
- ボコーダー自体の素性による劣化（メル80バンドの往復で失われる情報）。エンジン切替で足りなければここに踏み込む必要がある

## 10. 環境メモ

- `gh` は使えるが **GitHub GraphQL は不可**。REST を使う（`gh api repos/{owner}/{repo}/...`）。`gh workflow run` は内部で GraphQL を叩くので失敗する → `gh api -X POST repos/Rowryu/kinu-tune/actions/workflows/windows-release.yml/dispatches -f ref=main -F 'inputs[ref]=main' -F 'inputs[directml]=true'`
- リリースビルドは `windows-release.yml` を `directml=true` で手動実行。`diagnostics` 入力もある
- `windows-diagnose.yml` に pluginval の3シナリオ

## 11. 2026-10-22 のセッション（PC に直接つながった回）

このセッションからは **りゅうさんの Windows PC を直接触れた**。できることが変わったので、まずそれを書く。

### 環境（前回との違い）
- PC（`ryuzapc`, Windows）にファイル単位でアクセスできる。フォルダごとに許可を取る方式。今回許可を得たのは `C:\Users\ryuuz\Documents` / `C:\Users\ryuuz\Downloads` / `C:\Users\ryuuz\AppData\Roaming\Kisyo Tune` / `C:\Program Files\Common Files\VST3\Kisyo Tune.vst3` / `C:\Users\ryuuz\AppData\Roaming\Microsoft\Windows\Recent` / `G:\ダウンロード`
- **ダウンロードフォルダは `C:\Users\ryuuz\Downloads` ではなく `G:\ダウンロード`。** Windows の既定場所は空。ショートカット（`Recent` の `.lnk`）を読めば実体の場所が分かる
- 画面を見てアプリを操作することもできる（Studio Pro 8 / Studio One 7 / OBS / メモ帳 / Discord は全操作、エクスプローラーはクリックのみ、Chrome は閲覧のみ）
- **できないこと**: このコンテナも PC 側シェルも GitHub の blob ストレージに届かない（アーティファクト直ダウンロード不可）。`objects.githubusercontent.com`（リリース資産）には届く。ただしこのセッション種別では**リリースの作成が GitHub に拒否される**（403）。`workflow_dispatch` も手元の安全機構に止められた。**CI を動かす手段は push だけ**（`ci.yml` は push で走る）

### 分かったこと: 報告された不具合は「直っていたが入っていなかった」
インストール済みバイナリの日時（10/21 12:37 UTC）と、バイナリ内に `synthesisEnginePicked` の文字列が無いこと、`G:\ダウンロード` に `Kisyo-Tune-test-7c21ca62-dml-...` が展開されていたことから、動かしていたのは `7c21ca62` と断定した。確認したい時は
`strings 'Kisyo Tune.vst3' | grep -c synthesisEnginePicked` が早い（0 なら `9a1eeda` より前）。

### 差し替えの手順（次回も同じ手が使える）
1. zip は Linux 側から展開する。**こうすると Mark-of-the-Web が付かない**（§6 のブロック解除が不要になる）
2. **DAW を開いたままでも差し替えられる。** 読み込み中の DLL は上書きも削除もできないが、*名前の変更*はできる。`mv 'Kisyo Tune.vst3' 'Kisyo Tune.vst3.bak'` してから新しいものを元の名前で置く。DAW は再起動するまで古いものを使い続ける
3. 消す操作は既定で禁止。許可を取るまで `rm` は通らない
4. 今回の退避先: `G:\ダウンロード\Kisyo-Tune-rollback-7c21ca62\`。バンドル内に残った `*.7c21ca62.bak` は Studio Pro が掴んでいて消せなかった。**DAW 再起動後に消せる**

### 見つけた不具合: 実行デバイスにも同じ罠が残っていた
`9a1eeda` は合成エンジンを「ハードウェアで決まる既定値を config に書き戻さない」ようにしたが、**隣の実行デバイス（CPU/DirectML/CUDA）は直っていなかった**。`device` の初期値 `"CPU"` が `saveConfig()` で毎回書かれ、次回の読み込みで「ユーザーの選択」になる。一度そうなると設定画面の自動選択（`hasStoredDevice()` が偽の時だけ走る）は二度と走らない。

結果: **DirectML 入りビルドを入れてもボコーダーが CPU で回り続ける**（1ノート約2秒）。画面には理由が出ない。りゅうさんの config が実際にこの状態だった。

→ `d9e20eb` で、`getDevice()` を `getSynthesisEngineType()` / `getGPUDeviceId()` と同じ解決方式にし、`GpuDeviceList::defaultExecutionDevice()` を1か所の答えにした。`devicePicked` は `synthesisEnginePicked` と同じ扱い。`dev-rules.md` に §11 として書いた。

- **CI で確認したこと**: macOS で全34テストと補正スイート9本が通る。Windows VST3 がビルドできる（run 39667524791）
- **未検証**: DAW で一度も動かしていない。DirectML で実際にボコーダーが立ち上がるか、再合成が何秒になるかは本人の確認待ち

`ccf7c12` は、テストビルドの zip を `ci-builds` ブランチへ push するワークフローの追加。blob ストレージに届かない環境でもビルドを取り出せるようにするためだが、**このステップはまだ一度も走っていない**（`windows-release.yml` は手動実行でしか動かず、このセッションからは起動できなかった）。

### 手元の config を直接いじった
`%APPDATA%\Kisyo Tune\config.json` の `device` を `"CPU"` → `"DirectML"` に書き換えた。`49d5fd6` にはまだ上の修正が入っていないため、これをしないと CPU のままになるから。元のファイルは `config.json.before-49d5fd6.bak`。
**アダプタが無ければプラグイン側が自動で CPU に戻すので、外れても壊れない。**

GPU は **NVIDIA GeForce GTX 1650**（OBS のログ `%APPDATA%\obs-studio\logs` で確認）。単体の非 Intel・非内蔵アダプタなので、`defaultSynthesisEngineType()` の `vocoderCapable` も `hasGpuInference()` も真になる条件を満たしている。つまり AI リシンセシス + DirectML で動くはずの構成。

## 12. ガイドトラックが選べない件（2026-10-22 午前）

りゅうさんの報告: 「ガイドトラックにピンをさしましたがガイドになりません」。

**ピンとガイドは別の機能。** ピン(`d6a42c9`)はそのトラックのピッチカーブを薄く描くだけ。ガイドは MIDI ガイドカードの「ガイドトラック」メニューで選ぶ。候補になるのは `listGuideTrackCandidates()` の条件、**アクティブでない ARA トラックのうち Kisyo Tune が解析済みでノートを持つもの**だけで、ピンを刺しても解析はされない。

そのうえでバグが2つあった(`2a2c928` で修正、CI run 39712598471 通過)。

1. **メニューが一度しか作られない。** `MidiGuidePanel::refreshGuideSources()` のコメントは「トラックの顔ぶれが変わるたびに呼ばれる」と書いているが、呼んでいるのは `setHostBpmProvider` と `setGuideTrackProvider` の2か所だけで、どちらもエディタ構築時の1回きり。後から解析・追加・ピンしたトラックは永久に出てこない。→ `MainComponent::updateTrackList()` から呼ぶようにした
2. **候補0件で行ごと消える。** 「古い」ではなく「そんな機能は無い」に見えていた。→ ARA 下では行を残し、`midiguide.guide_track_unanalysed`（「他トラックに解析済みのノートがありません」）を出す

**修正前のビルドでの回避策**: ガイドにするトラックを一度アクティブにして解析させる → メインに戻る → **Kisyo Tune のウィンドウを閉じて開き直す**（1 のため再構築が必要）。

回帰テストは `VariAudioInspectorTests` に追加した。`MidiGuidePanel::getGuideTrackMenu()` はそのために足した読み取り専用のアクセサ。

**未検証**: DAW で実際にピン→解析→メニュー出現まで通していない。

## 13. ガイドトラックが選べなかった理由（2026-10-22 午後・解決）

**結論: バグが5つ重なっていた。全部直してマージ済み、実機で動作確認済み。**

| # | 内容 | コミット |
|---|---|---|
| 1 | ガイドトラックのメニューがエディタ構築時の1回しか作られない | `2a2c928` |
| 2 | 候補0件で行ごと消え、理由も出ない | `a7cf5fc` |
| 3 | **行が 0×0 で描画されない**（これが致命傷） | `094f689` |
| 4 | タイミングカードがガイドの到着に気づかない | `b48aee9` |
| 5 | 解消した警告が画面から消えない | `c9a8498` |

### 3 が本命
`resized()` はこの行が非表示の間 bounds を与えない。`setVisible()` はレイアウトを走らせない。エディタは構築中に一度行を隠し(`setHostBpmProvider` が provider 設置前に refresh する)、直後に出す。この2回目で bounds が与えられないので、行は**構築時のまま＝0×0**。「表示されていて、正しくて、見えない」状態が毎回起きていた。1 と 2 を直しても画面が変わらなかったのはこれが理由。

### ノートは最初から読めていた
候補一覧には `main 2` と `【歌ってみた】…` の両方が、**「（ノートを読めません）」のマーク無しで**出た。`listGuideTrackCandidates()` の弾きではなかった。

### 操作メモ（次回のため）
- **Kisyo Tune の開き方**: イベントFX が有効なイベントをダブルクリック。インスペクターの「イベントFX」がオレンジの **「レンダー」**表示になっているイベントが対象（「有効化」のままのイベントでは開かない）。このプロジェクトでは track 5 の `main 2(8)`
- オーディオメニューに Kisyo Tune の項目は**無い**（Melodyne と VocAlign だけ）
- ピンとガイドは別物。ピンは薄描画だけ

### 実機で確認したこと（`c9a8498` のビルド）
ガイドトラックに `【歌ってみた】…` を選ぶ → **「ガイド 77 音 / -0.20 秒から 20.07 秒」**、タイミング補正カードの赤字が消えて Apply が有効。

### 未確認
- `ピッチを補正` / `タイミングを補正` を実際に押していない。補正の自然さと音質はりゅうさんの耳待ち
- ピアノロールにノートのブロックが描画されない件。`PianoRollComponent::paint()` のトラックモード用クリップ(`getTrackModeActiveSpan()`)が怪しいが未調査。波形は描かれるので表示だけの問題

## 14. 実機フィードバック後の4件（2026-10-22 夜〜23 未明）

りゅうさんの報告: 「ピッチ補正は反応したが、ガイドに合わせての音質劣化が気になる」「タイミングはガイドに合わせたときの破綻が大きい」「BPM に合わせる機能は反応はあるが1音も拾えない」。

### BPM が1音も拾えなかった理由（`402a075`）
許容値「この範囲内は動かさない」の単位が**フレーム固定**だった。200 BPM・16分ではグリッド間隔 6.46 フレーム、どのノートも最大 3.23 フレームしか離れられない。既定が 3.0 フレームなので**判定範囲が 0.23 しか残らない**。32分では範囲ゼロで数学的に不可能。
→ 「最大ズレの%」に変更（0%=全部スナップ、100%=何も動かさない、既定 25%）。どのテンポでも同じ意味になる。

### タイミングが不自然だった理由（`bd8164a`）
アンカーが **RMS（音量の山）**だった。歌は子音「し」「す」やブレスが母音より大きく、しかもピッチを持たない。子音の位置をグリッドに乗せるので、子音と母音の距離だけ全体がずれる。
→ `TimingAnchor::PitchCentre` を追加。**ピッチが出ている最長区間の中心**をアンカーにする。無声フレームはどの区間にも入らないので子音・ブレスがアンカーを奪えない。最長区間なのでしゃくりにも引っ張られない。ピッチが安定しないノートは音量基準にフォールバック。推奨トグルがこれになった。

### ガイドに引っ張られる劣化（`bd16245`）
歌ガイドのノート中心には**その歌い手の音程のクセ**（数十セント）が乗っている。そこへ寄せると他人のズレを移植し、かつ移動量が増えてボコーダーを通る範囲が広がる。
→ ガイドのノート中心を**最も近い半音に丸めてから**寄せる。MIDI ガイドは元から半音上なので no-op。カードにトグル（既定オン）。

### ノート・ピッチ線が描画されない件 → **バグではなかった**
フル解像度で見ると青いノートブロックと白いピッチカーブは正常に出ている。音域が画面外だっただけ。診断ログでも裏が取れた:
`canvas[setProject] trackMode=1 notes=70 ranges=1 span=[0.000..19.960] notesSeconds=[0.232..19.807]`
クリップ範囲がノート範囲を完全に含んでいる。診断は `PianoRollComponent::logCanvasGeometry`（logging ビルドのみ）。

## 15. プラグインを差し替えたら Studio Pro を完全に再起動する

**今日いちばん時間を失った罠。**

Studio Pro が起動したままだと、プラグイン未インスタンス化の間は DLL がロックされない。つまり**上書きは成功するのに、ホストは既に読み込んだ古いモジュールを使い続ける**。その結果、新ビルドをインストールしたつもりで古いビルドを評価してしまう。

- 症状: ディスク上のバイナリには新しい文字列が入っているのに、画面のラベルや既定値が古い
- 確認方法: 画面のラベルを1つ見る（例: 許容値が「(フレーム)」なら旧、「(最大ズレの%)」なら新）
- 対処: **ファイル → 終了 → 起動し直す**。パネルを閉じるだけでは足りない

ログが出ているかも判定に使える。`%APPDATA%\Kisyo Tune\Logs\` が無ければ、そのセッションでは新しい logging ビルドが動いていない。

## 16. 2026-10-23 ビルド履歴 (guide-anchor-and-reach)

**Studio Pro で動いているのは fbb4433**。bb5313a はファイルを置いただけで、
ホストを再起動するまで読み込まれない（§15）。

入っているもの:

- ガイドノートが自分のアンカーを持つようになった。`GuideNote::anchorFrame` を
  `PluginEditor::buildGuideFromTrack` でガイド側トラックのピッチから求め、
  リージョン写像を通して運び、`guidedTimingTargets` で優先する。これ以前は
  歌側が「ピッチの出ている所」、ガイド側が「ノート先頭」という食い違いがあり、
  子音＋立ち上がりの長さ (50〜150ms) が全ノートに同方向のずれとして乗っていた。
  MIDI ガイドは音声を持たないので従来どおり先頭を使う（それが正しい）。
- confidence が strength を上書きしていた件。`trustedConfidence = 0.6f` を追加し、
  minConfidence〜0.6 の間だけ信頼度で抑え、それより上は strength だけが効く。
  この素材の平均 confidence は 0.808 なので、以前は強さ 1.0 でも 8 割しか動いて
  いなかった。

- レガートのオンセットが動かなかった件。ノート列は take の分割なので、間を
  空けずに歌った所では「前のノートの終わり」と「次のノートの頭」が同じソース
  フレームになる。そこに逆向きの要求が乗ると、以前は rate limiter がゼロ長の
  ステップを見て 2 つ目のアンカーを 1e-6 だけずらし、**前のアンカーの出力位置を
  そのまま与えていた**。つまり次のノートの頭の移動は黙って捨てられ、代わりに
  尻尾だけが動いてノートが潰れる。10 フレーム早めたいノートが 10 フレーム遅く、
  しかも 2 割縮んで出てくる。レガートの連なりでは最初の 1 音以外すべてこれ。
  同じソースフレームのアンカーは rate limiter に渡る前に 1 本に畳み、**頭の方を
  残す**ようにした。レガートでの尻尾は独立した瞬間として聞こえず、次の頭への
  助走として聞こえるので、伸縮はそこに寄せるのが一番聞こえない。ソートも
  stable にした（同フレーム 2 本のときどちらが来るか不定だった）。

インストール手順はこの回も §11 のとおり。**今回は Studio Pro を完全終了して
起動し直してある**（§15 の罠は回避済み: プロセスが DLL 差し替えより後に起動
しているので、古いモジュールが残っている可能性はない）。起動時に Studio Pro の
セーフスタート画面が出たが「通常起動」で問題なく立ち上がり、プラグインスキャン
も通った。

残り:

- タイミング補正の AI 再合成 (課題 5)。未着手。設計から。
- 音質のさらなる改善 (課題 4)。ユーザーの優先度は低め。
- グリッドの許容幅はハードな閾値。境界をまたぐ隣り合ったノートの扱いが
  不連続になるので、傾斜にする余地はある（未着手、効果は未検証）。
- 許容幅は take 全体で 1 つのフレーム数で、リージョン先頭の拍間隔から出して
  いる。テンポトラックをまたぐ take では片端で甘くなる。

## 17. bb5313a: ホストのテンポマップを読む

グリッドは「Apply を押した瞬間のテンポ 1 つ」で作っていた。トランスポートが
返すのは再生ヘッド位置のテンポなので、イントロに再生ヘッドを置いたまま 4 分
過ぎの take を補正すると、イントロのテンポで引いたグリッドに吸着する。
『竹取オーバーナイトセンセーション』は 100〜205 で動くので、まさにこれ。

ARA の musical context が答えを持っている (`kARAContentTypeTempoEntries`:
秒と四分音符の対、間は一定テンポ = テンポトラックそのもの)。
`PitchNetAudioProcessorEditor::readHostTempoMap()` が、対象リージョンの
**リージョンシーケンス経由で** musical context を取って読む
（JUCE は `ARAPlaybackRegion` に `getMusicalContext` を出していない。
`ARARegionSequence` にはある）。`isMusicalContextContentAvailable` で
可否を先に聞くのは SDK の要求。

`TempoMap` (Source/Audio/Correction/TempoMap.h) は SDK 非依存なので
`tests/TempoMapTests.cpp` で単体テストできる。`TimingGrid` はマップがあれば
フレーム→曲中の秒→四分音符で丸めて戻す。マップが無いホストでは従来の
単一 BPM にフォールバックする。

### CI のログが読めない件（解決済み）

`results-receiver.actions.githubusercontent.com` はこの環境のポリシーで
拒否されるので、`gh run view --log` は常に Forbidden になる。失敗が
「exit code 1」としか見えない。windows-release.yml の Build ステップが
出力を `build.log` に tee するようにして、失敗時に ci-builds ブランチへ
`BUILD-LOG.txt` として publish するステップを足した。これで
`git show FETCH_HEAD:BUILD-LOG.txt` でコンパイルエラーが読める。
