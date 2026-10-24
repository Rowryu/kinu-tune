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
- リリースビルドは `windows-release.yml` を `directml=true` で手動実行。`diagnostics` 入力もある。**テストビルドも必ず `-f directml=true`**（付け忘れると AI リシンセシスが選べなくなる。2026-10-24 にやらかした）
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

## 18. 構想: インスト（ドラム）のオンセットに合わせるタイミング補正

ユーザーからの提案。テンポマップでも詰め切れない場合の次の手として。

**枠組み**: これは新しいモードではなく、**グリッドの線をどこから持ってくるか**の
違いとして実装するのが素直。`gridTimingTargets` は「アンカーを最寄りの線に寄せ、
許容幅の内側なら動かさない」をすでにやっているので、線の供給元を
「BPM / テンポマップ」から「インストの音声から拾ったオンセット」に差し替えれば、
強さ・許容幅・アンカーの選び方はそのまま使い回せる。

ビートトラッキングでテンポマップを作る案もあるが、それは「意図されたグリッド」に
戻る話で、ユーザーが欲しいのは「実際に鳴っている位置」。オンセットに直接
合わせる方が目的に合っていて、しかも小さい。

**音声の取得**: ARA ではドキュメント内のオーディオソースしか読めず、ホストが
ソースを登録するのはプラグインが載っているトラックだけ。つまり**インストトラックに
Kisyo Tune を挿す必要がある** — 今の「歌のガイドトラック」と同じ条件なので、
運用としては既に慣れているはず。

**オンセット検出**: スペクトルフラックス（半波整流して帯域内で総和）→ 適応閾値 →
ピーク拾い。モデルは要らず、JUCE 非依存で書けるので単体テストできる。
解析フレームのグリッド (fps ≈ 86.13) に乗せればそのまま線になる。

**注意点**:
- インストが 2mix だと全楽器のオンセットを拾う（『リアライズ』のトラック 5 は
  `リアライズ_2mix_inst`）。キック帯 (30–200Hz) とスネア帯 (150–400Hz) に
  絞ったフラックスにすると、だいぶドラム寄りになる。
- 16 分のハイハットまで拾うと線が多すぎて「どのノートも何かの線の近く」に
  なり、補正が意味を失う。帯域制限に加えて最小間隔の制約が要る。
- 歌はドラムにぴったり合うべきものではないので、許容幅と強さは今と同じく要る。

未着手。テンポマップを試した結果を見てから。

## 19. 「シャリつく」の調査 (2026-10-23)

ユーザーの報告: 補正後が元音源よりシャリついて聞こえる。レガート修正は効いた
（「前よりも真ん中で捉えられてた」）。

### 第 1 容疑者: 子音がまるごとボコーダー再合成されている

`IncrementalSynthesizer::generateBlendMask` は、無声フレームの連続が
`kKeepOriginalUnvoicedFrames = 24` **以上**のときだけ元音声を残す。
24 フレーム = 24 / 86.13 ≒ **279 ms**。つまり 279ms より短い無声区間は
すべて合成音で置き換わる — **子音とサ行の摩擦音はまず全部これに入る**。

メル 80 バンドから /s/ /ʃ/ を再構成すると、ノイズ成分が作り直されるので
ざらついた・やや金属的な高域になる。「シャリつく」はまさにここ。

この定数が長いのは、ノート境界で orig/synth が櫛形干渉するのを避けるため
（コード中のコメントにそう書いてある）。トレードオフは本物なので、単に
短くするだけだと別の問題が出る可能性がある。ランプは 1024 サンプル (≒23ms)。

### 第 2 容疑者: 48kHz セッションでの再生時リサンプル

解析・合成は 44100 Hz 固定 (`Source/Utils/Constants.h`)。ホストのレートが
違うと `ARADocumentController.cpp` の再生経路が
`juce::LagrangeInterpolator` でリサンプルする。r8brain はこのリポジトリに
入っていて `Source/Utils/AudioResampler.cpp` で使われているが、**再生経路は
Lagrange のまま**。5 点ラグランジュは高域の補間誤差が大きい。

レートが一致するときは直コピーの速い経路に入るので、44.1kHz セッションでは
この経路を通らない。『ろすとわん』は **48.0 kHz**、『リアライズ』は 44.1 kHz。

注意: 補間器を差し替えると群遅延が変わる（Lagrange ≒2 サンプル、
windowed-sinc は数十サンプル）。読み出し位置で補正しないと、処理後トラックが
他トラックに対して数十サンプルずれる。

### 切り分けの結果

**第 2 容疑者（リサンプラ）は白。** ユーザーが 44.1kHz と 48kHz の曲で
聴き比べて「音質に大きな違いなし」。Lagrange のままでも今の劣化の主因では
ない。将来的に r8brain に替える価値はあるが、優先度は低い。
（替えるなら群遅延の補正を忘れないこと。）

**第 1 容疑者に対する手当て (e394a036)**: `kKeepOriginalUnvoicedFrames` を
24 → **6** に下げた。6 フレーム ≒ 70ms は、ランプ (1024 サンプル ≒ 23ms) が
両端に入ってなお芯が残る最小の長さ。これ未満の無声区間は、元音声が全開に
なる区間を持てず、元音声と合成音を中途半端な比率で足し続けるだけになって
櫛形干渉するので、従来どおり合成側に残す。ノート間の 1〜3 フレームの隙間
（元のコメントが心配していたケース）はここに入る。

タイミングが動いたフレームは別扱いで、`generateBlendMask` の**後**に走る
「リタイムされたノートを合成側に戻す」パスがすでに面倒を見ている
（動いた元サンプルは再生位置と対応しないため）。なので今回の変更で
タイミング補正済みの箇所に生サンプルが混ざることはない。

**これは実験である。** シャリつきが消えれば原因は子音の再合成で確定。
代わりにノート境界で櫛形干渉が出るなら、考え方ではなく閾値が低すぎた、
ということになるので 8〜10 あたりに上げる。

## 20. 境界の変な音 → クロスフェードの法則 (54644211)

閾値を 6 に下げたら、シャリつきの代わりに**境界で変な音が増えた**という報告。
原因は判断の方ではなくクロスフェードの方だった。

ブレンドは `b*synth + (1-b)*orig` の**線形**フェードだった。これは 2 つの信号に
相関があるときだけ正しい。ボコーダーの出力はメルから作り直したもので、位相は
元音声と何の関係も無い。**無相関な 2 信号はパワーで加算される**ので、線形
フェードだとクロスフェードの中央が 3dB 凹み、マスクが動くにつれて打ち消しが
スペクトル上を掃いていく。1 フレーズに 1 回なら我慢できたが、子音ごとに 1 回に
なったら我慢できない。それが「境界値での変な音」。

法則をエンジンで分けた:
- ボコーダー → 等パワー `sqrt(b)` / `sqrt(1-b)`（和が 1 に保たれる）
- PSOLA → 線形のまま。元波形を変形しているので相関があり、振幅で足すのが正しい

マスクが 0 と 1 の間にある区間しか変わらないので、完全合成・完全元音声の区間は
ビット単位で同一。

次の切り分け: 境界が綺麗になれば閾値 6 のまま子音は元音声で残せる。まだ駄目なら
閾値を 8〜10 に戻す（=考え方ではなく値の問題だった、ということ）。

## 21. タイミング補正が効かない件の切り分け

2026-10-23 のスクリーンショットで、ガイドモードが
「43 音のうち 38 音を動かしました（最大 118.1 ミリ秒）」と出ていた。
補正の強さ 0.50、最大の移動量 0.150 秒。強さ 0.5 で 118ms 動いたということは、
歌とガイドの生の差が **240ms 近い**ということになる。これは「少しずれている」
ではなく「別の音に対応づけている」か「ガイドが全く違う歌い回し」のどちらか。

つまり、ドラム対応を作る前に確かめるべきことがある:
**悪いのはターゲットの選び方か、伸縮そのものか。**
強さを 0.2 くらいまで落として聴き、「自然だが直りきっていない」なら伸縮は
無実でターゲット選びの問題。「それでも変」なら `buildTimingMap` の伸縮自体が
問題で、ドラムに変えても同じ壁に当たる。

## 22. 「ボーカルの裏の囁き声」(400c6a75)

54644211 を聴いての報告: ピッチ補正のノイズは減ったが、**ボーカルの裏に
囁き声のようなものが入る**。

正体はブレンドの受け渡し区間。そこでは合成音と元音声が**同時に鳴っている**。
無声の境目では両方とも息の音なので、**同じ息の違うテイクが重なって聞こえる**。
それが囁き声。等パワーにしたことで両方が -3dB で居座るようになり（線形だと
片方が勝つ）、さらに閾値 6 で境目の数が増えたので、目立つようになった。

ランプを 1024 サンプル (23ms) → **384 サンプル (8.7ms)** に短縮。境目の内容は
ノイズなので、速い受け渡しはクリックにならず隠れる。重なる区間が 1/3 になる。
6 フレーム (70ms) の最短ランの中にランプ 2 本が余裕で収まるので、子音は元音声の
ままで残る。

次: 薄くなったがまだ居るなら、短くするのではなく**境目の数を減らす**方向
（無声の閾値を 8〜10 に上げる）。消えていれば子音はこのまま。

## 23. 伸縮の上限 (400c6a75 に同梱)

「強さを上げると音が伸び縮みして聞こえる」。ソロで聴いて音の方が変、という
回答だったので、ターゲットではなく伸縮が原因で確定。

`minRate 0.5 / maxRate 2.0` のまま誰も狭めていなかった = **2 倍に伸ばすのも
半分に縮めるのも許可**されていた。グリッドやガイドで動かされる音自体は伸縮
しない（頭と尻が同じだけ動く）ので、移動量は全部「隣との間」が吸収する。
レガートでは間が無く、前の音の尻尾が全部引き受ける: 50ms ずらすと 150ms の
隣が 1/3 縮む。

**0.85 / 1.18**（前後 1/6）に変更。超える要求は歪めてでも従うのではなく
途中で止まる。既存テスト 5 本が「ターゲットにぴったり着く」を検証していて
上限と絡んでいたので、各テストが自分で上限を開けるようにした。

次: 「自然だが足りない」なら上限を開ける、「まだ伸び縮みする」なら締める。

## 24. ぶつぶつノイズ → 等パワーの式が悪かった (9454cc5f)

400c6a75 を聴いての報告: 囁き声は減ったが**ぶつぶつノイズが激増**。

原因は等パワーの書き方。`sqrt(b)` / `sqrt(1-b)` は確かにパワーを 1 に保つが、
**sqrt は 0 から垂直に立ち上がる**。フェードの両端でゲインが無限の速さで動く
= 実質ステップ。23ms のランプでは角が鈍って気づかなかったが、8.7ms に縮めた
ことで角と角が近づき、しかも子音ごとに継ぎ目があるので連打になった。
**ランプを短くしたからクリックが出たのではなく、元々あったクリックが露出した。**

`sin(b*π/2)` / `cos(b*π/2)` に変更。これも厳密に等パワーだが、**両端の傾きが
ゼロ**なので角が無い。ランプは 8.7ms のまま（囁き声対策の効果を残すため）。

PSOLA は従来どおり線形。線形には元々角が無い。

### このループで分かったこと（次に同じ道を通らないために）

ブレンドの継ぎ目には 3 つの失敗モードがあり、互いにトレードオフになっている:
1. 継ぎ目が長い + 両方が鳴る → 息が二重に聞こえる（囁き声）
2. 継ぎ目が短い + ゲインに角 → クリック（ぶつぶつ）
3. 継ぎ目が無い（全部合成） → 子音がメル再合成されてシャリつく

1 と 2 は「法則」の問題で、両方とも直せる（角の無い等パワー + 短いランプ）。
3 だけが本質的なトレードオフ。閾値 6 は 3 を避けるための値。

## 25. ぶつぶつの本当の原因 (20a82758)

sin/cos にしてもぶつぶつは消えなかった。つまり段差はフェードの中ではなかった。

`generateBlendMask` の**後**に走るパスが犯人。タイミング編集のあるノートの
区間を `blendMask[sample] = 1.0f` と**直書き**していて、両端に一切の助走が
無かった。

マスクがほぼ全域 1.0 だった頃は無害だった。子音を元音声に戻したことで無声区間の
マスクが 0 になり、その隣に 1.0 を直書きすると**1 サンプルで元音声から合成音に
切り替わる**。リタイムされたノート 1 つにつきクリック 2 発。

さらに伸縮の上限を狭めたことで悪化した。一見無関係だが辿ると繋がる:
**上限で完全に弾かれていたノートが「少しだけ動く」ようになったので、
リタイム扱いのノートが激増し、その分だけ段差が増えた。**

ブレンドと同じランプでマスクを 1.0 まで持ち上げるようにした。助走はノートの
**外側**で使うので、リタイム済み音声が位置の合わない生サンプルと混ざることは
無い。マスクは上げるだけで下げないので、隣のノートが確保した区間を壊さない。
ランプ長は `kBlendRampSamples` 1 箇所に統一した — **マスクは複数のパスで
書かれていて、どこか一箇所でも段差があればクリックになる**。

次にまだ残るなら、容疑はマスクの段差ではなく**継ぎ目でのレベル不一致**
（合成音と元音声の音量が合っていない）に移る。別の直し方になる。

## 26. 「音質が悪い、メインの補正には使えない」(2026-10-24 02:07)

20a82758 でぶつぶつは消えた。残っているのは**母音の音質**。

### ここまでの継ぎ目作業が届いていなかった範囲

§20〜§25 で直してきたのは全部**無声区間の継ぎ目**の話だった。しかし
`generateBlendMask` は `frameMask(numFrames, 1.0f)` で始まる。つまり
**合成範囲に入った有声フレームは、編集の有無にかかわらず全部メル再合成**
される。母音は音の大半で、そこは最初から一度も元音声に戻していない。
継ぎ目をいくら綺麗にしても母音の音質には届かない。

### 取れる範囲がどれだけ広いか

`getDirtyFrameRange` → `selectConnectedEditedNotes` で連結クラスタに拡張 →
`computeSynthesisRange` で有声区間ごと取り込み、16 フレームのギャップを橋渡し、
前後 24 フレームのパディング。**take 全体にピッチ補正をかけると take 全体が
再合成される。**

`PitchTargetParams::minShiftSemitones = 0.03f`（3 セント）のデッドバンドは
あるが、ガイドに合わせる補正では大半のノートがこれを超えるので、実質
全ノートが再レンダされる。

### 考えられる方向（未着手、要相談）

1. **エンジンを変える**。PSOLA は元波形を変形するのでメルを通らない。
   「多少マシ」との評価だったが、それは**タイミング補正込み**で聴いた
   可能性がある。PSOLA + ピッチのみ + 数ノートだけ補正、で切り分ける価値あり。
2. **再レンダする範囲を減らす**。
   a. 編集の無いノートを元音声に戻す（マスクを 0 に）。
      注意: ノート境界が有声同士だと、位相の飛びが母音の途中で起きるので
      無声の継ぎ目より遥かに悪い。**無声区間で挟まれたノートに限る**べき。
   b. デッドバンドを可聴域ぎりぎりまで上げる（3 セント → 10〜15 セント）。
      「もう合っている音」は触らない。ユーザー可変にする手もある。
3. **メインの補正には使わない**と割り切る。


### 追記: エンジンの既定値が犯人の可能性が高い

`PsolaSynthesizer.h` の冒頭にこう書いてある:

> PSOLA never leaves the time domain. ... Nothing is re-estimated, so a ratio
> of 1.0 is a copy

つまり**クラシック (PSOLA) では、編集していない音声はコピー＝完全に元のまま**。
位相ボコーダーの方は「ratio 1.0 でも誤差が信号より大きい」と同じ箇所に
書かれている。母音の phasiness の出どころ。

そして `defaultSynthesisEngineType()` は **DirectML が使える Windows では
ボコーダーを選ぶ**。ユーザーの環境はこれに該当するので、**ずっと
「AI リシンセシス」で聴いていた可能性が高い**。だとすれば「ピッチ補正のみでも
劣化」は当然で、エンジンを変えるだけで状況が変わる。

要確認: ユーザーが今どちらのエンジンを選んでいるか。クラシックで
ピッチのみを試した結果が欲しい。

もしクラシックで解決するなら、**補正用途の既定値をクラシックにする**のが
筋。ボコーダーは大きな変形用として残す。

## 27. 参考: ほっとみるく中村 (@account_npc) の KinuTune カスタマイズ

ユーザーが「機能と UI がかなり理想に近い」として共有。
https://x.com/account_npc/status/2113734192324546768 の 2 分動画。
同じ KinuTune(Keinoaza) 派生。プラグイン名は "Hot milk pitcher"、ホストは
Cakewalk Sonar。章立てで機能を紹介している。

- 00:04 カスタマイズ内容（左パネル: Tools / スナップ・スケール / ハモリ /
  Humanizer）
- 00:06 画面の移動
- 00:18 **参考音源の波形表示** — レイヤーとして複数持てる。各レイヤーに
  「波形を表示」チェックとゲイン (1.0x)。「DAW のトラックカラーを使用」。
- 00:28 **波形を見ながらタイミング調整** — ノートの背後に参考トラックの波形を
  描き、目で見て合わせる
- 00:38 タイミング編集中もピッチ調整（簡易的）
- 00:48 **選択範囲をまとめて音程補正** — 範囲選択 → ダブルクリックで最寄り
  スケールに一括吸着
- 01:08 **レイヤー同期・補正**（右パネル）— 入力源を選ぶ / 位置・長さ・最大ズレ
  のスライダー / 「再生カーソルの位置を合わせる」/ 解析・適用・戻す /
  **補正対象（ピッチのみ・タイミングのみ・両方）+ 適用率 %**
- 01:28 **ハモリの作成・試聴** + **Humanizer**（セント・dB・時間のばらつき、
  「別パターン」「リセット」）
- 01:38 本人のコメント: **「音の劣化が本当に少ないのだ」**
- 01:50 波形表示のオン・オフ（複数レイヤーのピッチ曲線を色分けで重ね表示）

### ここから持ち帰るべきもの

1. **参考トラックの波形をノートの背後に描く。** 今のガイド補正は自動対応づけを
   信じるしかなく、実際 118ms も動かして外していた。**見えるようにして人間が
   確認・微調整できる**方が、マッチャを賢くするより先に効く。
2. **解析 → 適用 → 戻す**という段階的な操作。今の一発 Apply より安全。
3. **補正対象をピッチ/タイミング/両方で選べる**のと**適用率**。
4. 本人が「劣化が本当に少ない」と言っている。同じコードベースなので、
   **エンジン選択（クラシック/AI）か適用範囲の違い**である可能性が高い。
   §26 の未解決の問いと繋がる。

### 前編 (10/21): https://x.com/account_npc/status/2112613154350154054

32 秒。「Windows対応させてGPU処理などを追加。波形とノートのMIX表示、
ショートカット変更。コレマジですごいわメロダインじゃなくていいんじゃね？
ハモリがすご過ぎ。」

- **波形とノートの MIX 表示** — テイク自身の波形をピッチ曲線・ノートの背後に
  グレーで描き、その上にシアンのピッチ曲線。
- 設定ダイアログ（タブ: 一般 / ショートカット）: デバイス **DirectML**、
  GPUデバイス **NVIDIA GeForce RTX 3070**、ピッチ検出器 **RMVPE**、
  あとはデバッグ表示のチェックボックス群。
  **合成エンジンの選択肢はこのダイアログには無い**（うちと同じ系譜の設定画面）。

### うちの現状との差分

- テイク自身の波形背景は**すでにある**（`WaveformBackgroundRenderer`、
  `PianoRollComponent` が描画）。MIX 表示相当は実装済み。
- **無いのは「他トラック（ガイド／インスト）の波形をレイヤーとして重ねる」方**。
  ARA で他トラックのノートは読めている (`listGuideTracks`/`buildGuideFromTrack`)
  ので、同じドキュメントのオーディオソースから波形も取れるはず。
- 合成エンジンの切り替えは `ParameterPanel` の**トグルボタン 2 つ**
  （`param.engine_vocoder` = 「AI リシンセシス」/ `param.engine_psola` =
  「クラシック」）。設定ダイアログではなくパラメータパネル側にある。
  ユーザーが見つけていない可能性がある。

## 28. クラシックでも劣化する → 仮説は外れ (2026-10-24)

ユーザー確認: **クラシック (PSOLA) でピッチのみでも音質が変化する。**
§26 の「エンジンの既定値が犯人」は外れ。両エンジンに共通する何かが原因。

コードを追った範囲での事実:
- `Project::getAdjustedF0ForRange` = `midiToFreq(basePitch + deltaPitch +
  globalPitchOffset)`。`PitchCurveProcessor` の分解は
  `deltaPitch = midi(source) - base` なので **base + delta = midi(source) で
  ロスレス**。つまり未編集フレームの `buildPitchRatioCurve` の比は
  freq→midi→freq の往復誤差（~1e-6 相対）だけで、ほぼ厳密に 1.0。
  → 理屈の上では PSOLA はコピーになるはず。
- それでも変わるということは、原因はピッチ比の外側にある。
  未確認の候補: `TransitionSmoothing`（ノート境界の F0 整形）、
  `NoteGainCurve`、`FormantShifter`、合成範囲全体の書き戻し。

**ここから先は推測を重ねず測るべき。** ユーザーに「補正前」「補正後」の
2 ファイルを書き出してもらい、差分スペクトルで劣化の場所と性質
（広帯域か高域か、持続母音かノート境界か）を特定する。

## 29. ピン留めトラックの波形表示 (e9604b84)

ユーザー要望「UI を動画のように改善して」への第一歩。

**ほとんど既にあった。** ピン留めトラックのリージョンはすでにプレビューとして
キャンバスへ publish されていて、`buildRegionPreview` が音声も詰めている。
波形レンダラがそれを見ていなかっただけ — `PianoRollComponent` が
`withoutPinnedRegions()` で落としてから渡していた。

- レンダラにピン留めリージョンも渡すようにした
- ピン留めの波形を**先に**（自分の波形の下に）、**そのトラックの色**で描く
- `envelopeAt`（「ここに自分の音があるか」の判定）からはピン留めを除外
- DAW カラーのスイッチの下に **「ピン留めトラックの波形を表示」** スイッチ。
  `showPinnedWaveforms` として config に永続化。

残り（動画にあって未実装）: レイヤーごとのゲイン、解析→適用→戻すの段階的な
操作、補正対象（ピッチ／タイミング／両方）の選択と適用率。

## 30. 実測: 補正前後の 2 本を突き合わせた結果 (2026-10-24)

ユーザーが 26 秒・48kHz・24bit のモノラル 2 本を提供（他のプラグインは全部外した
状態）。`7de50937` と `64eb9256`。どちらが補正後かは未確認だが、
**`7de50937` が補正後**と推定（根拠は下）。

### 測ったこと

- 包絡線で 1ms 分解能の相互相関 → **一貫して 6ms (288 サンプル) のずれ**
  （相関 0.92）。レイテンシ申告の疑い。要確認。
- 1 秒ごとの RMS は 26 秒間ずっと約 1dB 差で平行 → 同じテイク・同じ演奏。
- 波形相関はほぼ 0。位相無視のスペクトル一致度も素のままでは 0.65。
- **ただし ±40ms の範囲で局所的に合わせ直すと一致度は中央値 0.89、
  上位は 0.9998 まで上がる。** つまり素材は同じで、比較の仕方の問題だった。
  （探索で出る ±33ms の揺れは、2048 点窓のスペクトルが数十 ms 定常なことによる
  探索ノイズと解釈。包絡線ベースの遅れは一貫して 5〜7ms なので、
  **タイミング補正は掛かっていない**と読める。）

### よく一致する区間 (一致度 > 0.98、62 窓) での帯域差

300-1000Hz で揃えた後:

| 帯域 | 差 |
|---|---|
| 100-300 Hz | -0.06 dB |
| 300 Hz-14 kHz | **±0.5 dB 以内** |
| 14-18 kHz | **-2.64 dB** |
| 18-22 kHz | **-4.80 dB** |

→ **音の本体（14kHz 以下）は音色的にほぼ無傷。壊れているのは「空気感」だけ。**

### もう 2 つの差

- **静かな区間が 12dB 静か。** 300-3kHz の下位 25% フレームで、
  補正後 8.5dB / 補正前 20.4dB。フレーズ間の息・部屋鳴りが抑えられている。
- **スペクトルが一致していても波形は一致しない** → 位相が作り直されている。
  スペクトルが「正しい」のに芯が無い、いわゆる phasiness の正体。

### 読み取れること

音色（14kHz 以下）は保たれているのに音が悪い、という訴えと符合するのは
**位相の作り直し**と**息の消失**の 2 点。どちらも「有声フレームを全部
作り直している」ことの帰結で、`generateBlendMask` が
`frameMask(numFrames, 1.0f)` で始まる件 (§26) そのもの。

14kHz 以上の落ち方（-2.6 / -4.8 dB の緩い傾斜、22.05kHz に崖は無い）は
**`juce::LagrangeInterpolator` の通過帯域のたれ**と形が合う。48kHz
セッションでのみ通る経路 (§19 第 2 容疑者)。崖が無いのは Lagrange に
アンチエイリアスが無いため。白と判定したのは早計だった。

### 確定 + 三つの自己訂正

**`7de50937` が補正後**で確定。半音からのずれが中央値 17.3 セント 対 28.1、
10 セント以内の割合が 32.8% 対 19.6%。ピッチが寄っている方が補正後。

**訂正 1: 「一致度 0.65」は誤り。** 揃え方が雑だった。±40ms で局所的に
合わせ直すと中央値 0.906、最良 1.000。
**訂正 2: 「位相が作り直されている」は誤り。** 整列後の波形相関は
よく一致する窓で中央値 0.92、最大 0.998。**位相は保たれている。**
**訂正 3: 「静かな区間が 12dB 静か」は誤り。** 20ms ブロックの下位 10% を
秒ごとに見ると、演奏中は補正後の方が 1〜2dB **大きい**。12dB は
0 秒目と 24 秒目（編集範囲外で補正後が完全無音、元は室内ノイズ −83dB）を
含めたせい。息は消えていない。

### 残った確かな事実

- 14kHz 以下は **±0.5dB 以内**。音色の本体は無傷。
- 14-18kHz で **-2.6dB**、18-22kHz で **-4.8dB**。Lagrange の通過帯域のたれと
  形が合う (§19)。48kHz セッションのみ。
- 全体で約 +1dB、**6ms 遅れ**。
- 編集範囲の外（頭と末尾）が**完全無音**。元は室内ノイズがある。

## 31. 本命: 補正が動かしすぎている

一致度の分布は 0.07 〜 1.000 と極端に割れる。**悪い所と良い所を時刻で特定し、
その時刻のピッチ差を測った結果:**

| 一致度 | ピッチ差 |
|---|---|
| 0.997〜1.000（最良） | **0 〜 21 セント** |
| 0.07〜0.25（最悪） | **+70、+81、+93、+165、+244 セント** |

**動かしていない音は完全に素通ししている**（一致度 1.000、0 セント）。
**聞こえる劣化は、実際に動かした音に集中していて、しかも 1〜2.4 半音
動かしている。**

ボーカルを 1〜2 半音動かせば、どんなエンジンでも「加工した音」になる。
これは DSP の不具合ではなく**補正が効きすぎ／対応づけを間違えている**。
118ms のタイミング移動と**同じ根**。

`PitchTargetParams::maxShiftSemitones = 2.0f` — 実測の +244 セントは
**この上限に張り付いている**。±30 セントで歌えている人に対して 2 半音の
「補正」が出るのは、まず間違いなく対応づけの誤り。

### 次の一手

1. `maxShiftSemitones` を 0.5〜0.7 半音程度まで下げる。それを超える要求は
   対応づけの誤りとみなして**動かさない**。
2. 動かさなかった音が見えるようにする（§29 の波形表示 + ガイドの可視化）。
   人が見て直せるなら、マッチャが完璧である必要はない。

注: f0 推定は 600Hz を上限にしたのでオクターブ誤判定がある。
-1050 セントの行は信用しない。+70〜+244 セントは上限未満で信用できる。

## 32. 合成経路を上流に戻した (d9821eb6)

ユーザーの指摘「元のエンジンから音が大きく外れてきている気がする」は**事実だった**。

`git show aadb76b:Source/Audio/Synthesis/IncrementalSynthesizer.cpp` と
上流 `keinoaza666-stack/kinu-tune` の同ファイルの差分は **0 行**。
つまり**今日より前は上流とバイト単位で同一**で、現在の差分 115 行は全部
今日のセッションで僕が入れたもの（§20〜§25 の 4 つの変更）。

それぞれが前の症状を別の症状に置き換えただけだった:
シャリつき → 囁き声 → ぶつぶつ。そして §30 の実測はそれらを正当化する材料を
出さなかった（出たのは「触っていない音は完全に素通ししている」という、
ブレンドが元々問題ではなかったことを示す結果）。

**このファイルを上流そのままに戻した。** 部分的にほどかず丸ごと。
作業内容はブランチの履歴と §20〜§25 に残っている。

`PitchTargeting` の「ガイドから遠すぎる対応づけは飛ばす（上限まで
引きずらない）」だけ残した。音色ではなく正しさの問題なので。
`maxShiftSemitones` は元の 2.0 に戻した。
**「動かす量が多いから劣化するのは仕方ない」という推論は取り下げ。**
ピッチを動かせばスペクトルが変わるのは当たり前で、劣化の証拠にならない。
Melodyne が同じ量を綺麗に動かせる以上、成立しない（ユーザー指摘）。

### 併せて計測したこと

- フォルマントは**保たれている**。ずらした 70〜244 セントに対し、
  スペクトル包絡の最適シフトは 0〜75 セント。チップマンク現象ではない。

### hot milk pitcher の公開状況

GitHub に無い。`keinoaza666-stack/kinu-tune` のフォークは **Rowryu/kinu-tune
の 1 件のみ**。コード検索でも該当なし。投稿にもリポジトリへのリンク無し。
→ UI は §27 の動画読み取りを元に自前で実装するしかない。

## 33. UI: ノート位置へ自動スクロール + グリッド表示 (d9821eb6)

- `PianoRollComponent::centreOnNotes()` を追加し `setProject` から呼ぶ。
  キャンバスは MIDI 全域を持つのに歌は 2 オクターブしか使わないので、
  開くたびに歌のない帯が映っていた。`restoreViewportState` は後から走るので
  ホストが保存したビューはそちらが勝つ。未解析のリージョンは触らない。
- グリッドの細かさを**鍵盤の上の空きストリップ**に `1/16` 形式で表示
  (`TimelineRenderer`)。キャンバスが実際にスナップしている値をそのまま出すので
  表示と挙動が食い違わない。

## 34. バイパス時の表示 (b9de0202)

音としてのバイパスは**元から入っていた**（標準のホストバイパスパラメータ
`PARAM_BYPASS` と、`processBlock` での素通し）。無かったのは表示の方で、
バイパス中もキャンバスは編集後を描き続けていた。

バイパス中は各ノートを**解析された位置**（`getSrcStartFrame/EndFrame` と
`getOriginalMidiNote()`、ピッチオフセット無し）に、**専用の青灰色**で描く。
元位置のゴーストはバイパス中は抑制（バイパス中のノートが元位置そのものなので、
同じものを二重に描くことになるため）。

エディタは既存のタイマーで `isBypassEngaged()` をポーリングする。ホスト
パラメータにはコールバックが無く、この経路なら DAW のバイパスボタン・
オートメーション・将来の自前スイッチが全部同じ道を通る。状態が変わった時
だけ `mainView->setBypassed()` を呼ぶので再描画は切り替え 1 回につき 1 回。

**未実装: プラグイン内のバイパススイッチ。** 今はホスト側のバイパスが唯一の
操作口。ParameterPanel のエンジン行もツールバーも、見ないと調整できない
レイアウト計算を含むので、画面を見られる状態で足すべき。

## 35. 残っている UI 要望（ユーザー 2026-10-24 13:09）

1. **左クリックでツール一覧をカーソル手元に出す** — 未着手。
   `CanvasPopupMenu` が既にあるので土台は使える。左クリックは編集に使っている
   ので、どのトリガにするか（修飾キー・中クリック・空白部）を要確認。
2. **タイミング補正中にもピッチ補正** — 未着手。動画 00:38 の
   「タイミング編集中もピッチ調整（簡易的）」に相当。
3. **バイパス** — 表示は §34 で対応。プラグイン内スイッチが残り。
4. レイヤーごとのゲイン、解析→適用→戻す、補正対象の選択と適用率、
   選択範囲の一括スケール吸着（§27 から）。

## 36. ピン留めトラックへの切り替えでピンを入れ替える (55905821)

ピンは「作業中のトラックの隣にこれを出しておいて」という意味なのに、
それを辿ると意味が壊れていた: 切り替え先はピンを失い（これは正しい）、
**後にしたトラックには何も付かない**ので、参照が消えて毎回手でピンを
付け直すことになっていた。

ピン留めトラックに切り替えると、**後にしたトラックがピンを受け取る**。
切り替え先のピンを落とす方はリスト再構築の所に元からあるので、足したのは
もう半分だけ。切り替え先がピン留めされていた時だけ動くので、普通の非アクティブ
トラックへの切り替えは今までどおり。

## 37. 右クリックのツールメニューは既にあった

`PianoRollComponent` の `mouseDown` に、`e.mods.isPopupMenu()` で
`canvasPopupMenu::Content` を**カーソル位置に**出す実装が既にある
（`getTargetArea(e.getScreenPosition())`）。中身は
元に戻す / やり直し / すべて選択 と、**4 つのツール**（ピッチ補正=Select /
ノーツカット=Split / ピッチライティング=PitchDrawing / タイミング補正=Timing）
のアイコン行。

ユーザーが追加で挙げた **ドリフト・平坦化・音量調整** は `EditMode` ではなく
`PitchToolHandles::HandleType`（`PitchDrift` / `Vibrato` = カーブのストレート化 /
`Amplitude`）で、**選択セグメントの上に出る小さな四角（スマートコントロール）**。
VariAudio と同じく「掴んでドラッグする」もので、モードではない。
`Options::showAll`（VariAudio の "Show All Smart Controls" 相当）はある。

→ メニュー項目にした時に何をすべきかが未確定。要確認:
  (a) そのスマートコントロールを出す／全部出す
  (b) 現在値で一発適用する（平坦化は `inspector.flatten_curve` があるので可能）
  (c) ドラッグの対象を切り替えるモードにする

## 38. UI batch, 2026-10-24 (user away, option ①)

- **Smart controls at any zoom.** With Show All (default on) a segment too small
  for corner controls gets every control in a strip: above = tilt L, straighten,
  drift, tilt R; below = formant, correct pitch, volume. Pitch drift finally has
  a handle (it had a drag but was never placed). Without Show All the old
  "zoom in first" rule stands. Tests: VariAudioControlTests.
- **Right-click menu**: three text rows ドリフト / 平坦化 / 音量 under すべて選択.
  Picking one switches to Select and arms `armedSmartControl`; a plain vertical
  drag on any segment then drives that control via
  `PitchToolController::beginDrag` (same group-edit and undo as the handle).
  Any edit-mode change disarms. Cursor is up/down over segments while armed.
- **Timing tool pitch edit**: vertical drag on a segment body (not a boundary)
  previews an outline with +N and commits whole semitones on release through
  `shiftNotesBySemitones` (one undo step).
- **Toolbar bypass**: power icon at the end of the transport capsule
  (Resources/images/bypass.png, generated). Sets the `bypass` APVTS parameter
  with a gesture; the editor timer's poll drives both the canvas and the
  button state, so host bypass and this switch stay in step.
- **Timing tool body drag (2026-10-24, after the user's 手 Melodyne videos)**:
  the drag locks to an axis after 4 px. Horizontal = move the segment whole
  (both its boundaries selected and shifted together, so the segment keeps its
  length and only neighbours stretch — "ノーツを伸縮させずにタイミングを変える").
  Vertical = whole-semitone pitch move. Cursor is the grab hand over a body.

## 39. Lessons from 手's Melodyne videos (read off on-screen text, 2026-10-24)

The videos have no narration; the explanation is on-screen text, so the
YouTube transcripts hold only lyrics. Read by stepping frames in the built-in
browser. Paraphrased takeaways and what they map to:

- Head/tail of a note (steep AND quiet) barely affects perceived pitch; only
  the body is corrected. Pick the intended centre by where the voice is
  loudest. → **done**: `correction::perceivedCentreMidi` (NoteCentre.*,
  tests/NoteCentreTests.cpp) = loudness-weighted median, steep+quiet frames
  weighted ×0.02. Used at analysis for each note's midi (falls back to the
  detector value if they differ by ≥ 1 st). deltaPitch keeps the shape, so
  scoops move with the note as in Melodyne.
- Robotic "kero" comes from right-angle transitions; smooth junctions after
  splitting/correcting. → TODO: auto-smooth boundaries of moved notes.
- Pitch-flatten tool: use sparingly. Within a band around the centre is fine.
- Timing: align the start of the pitch line to the grid; subdivide the grid if
  nothing is near. For some consonants (plosives, ん) use the amplitude rise.
  Stretching た/か-row consonants degrades them → split the consonant off
  first. → TODO: consonant protection in the timing map (rigid shift of
  onset→voiced-start, stretch absorbed by the previous vowel).
- **Consonant protection — done.** `TimingTarget::rigidHeadFrames` (from
  `rigidHeadFrames()`: frames to the first voiced frame + 3 attack, capped at
  40 % of the note) adds an anchor flagged `rigidWithPrevious`; the rate
  bounds keep it exactly at the onset's shift, so the head is copied at rate 1
  and the stretch lands in the sustained vowel / previous tail. Test:
  `testAConsonantHeadMovesRigidly`.
- **Correction to the above:** the timing map only decides where note edges
  land; the audio inside a note is rendered by `retimeMelInterval` /
  `retimeSourceFrameInterval`, which stretched each note linearly. So the map's
  rigid head alone did nothing audible. The real fix is in the renderer:
  `retime::sourcePosition` (Synthesis/RetimeMapping.h, tests/RetimeMappingTests)
  copies the head (`retime::headFrames`: to the first voiced frame + 3, ≤ 40 %)
  one to one and stretches only the rest. Applies only to notes whose length
  changed; a plain move is untouched. Both engines go through these two calls.
- Video (2nd half): split syllables sung together into separate notes and snap
  each; tight "ん" endings matter. → next: detect syllable onsets inside a note
  (unvoiced run / RMS dip) and split there before grid timing.
- **Syllable split before grid timing — done.** `findSyllableOnsets` (unvoiced
  run ≥ 2 frames, or RMS dip to ≤ 45 % of the louder side; ≥ 9 frames from
  edges/each other; tests/SyllableOnsetTests) → `planSyllableSplits` →
  `PianoRollComponent::splitNotesAtFrames` (NoteSplitter, one undo per split),
  run from the timing apply when grid mode and the new toggle
  「つながった音節を分けてから合わせる」 (default on). Only notes not yet moved.
- **Cubase AudioWarp video (SAKASAKAmax, burned-in subtitles):** warp tab at
  the syllable's waveform start, drag to a 1/32 grid; snap on/off is a matter
  of taste (he prefers off, checking with the metronome); the BPM/time
  signature must be right first; finding the true start is the skill. →
  timing-tool drags (boundary and whole-note move) now snap the dragged edge
  to the timeline grid when grid snap is on; holding **Shift** during the drag
  places it freely (Alt was first choice but Alt-click splits and Alt-drag
  is vertical zoom, both handled before the timing handler).
