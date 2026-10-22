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
