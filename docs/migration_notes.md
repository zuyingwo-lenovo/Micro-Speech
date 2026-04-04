# Micro-Speech Migration Notes
    
## 分析結果と旧実装からの移行方針

既存の `train_speech_model.ipynb` および関連コードの分析結果について以下にまとめます。

### 1. 何が古い・危険か
- **TensorFlow のバージョン**: `tf-estimator-nightly==1.14.0.dev2019072901 tf-nightly-gpu==1.15.0.dev20190729` がハードコードされており、これはプレ TF2.0 時代の非常に古いバージョンです。現行環境でのインストールは極めて困難かつセキュリティ・互換性リスクがあります。
- **モデルの記述やスクリプト**: TFリポジトリ内の古いサンプルコード (`tensorflow/examples/speech_commands/train.py`) に依存しています。このコードは現在の TF 2.x のベストプラクティス (Keras) を使っていません。
- **グラフのフリーズ (`freeze.py`)**: TF1 における凍結グラフ (Frozen Graph, `.pb`) の概念であり、TF 2.x では `SavedModel` が標準であり、直接 `tf.lite.TFLiteConverter` に引き渡す方式に移行しています。
- **TFLite 変換 (`toco`)**: `toco` (TensorFlow Lite Optimizing Converter) は廃止されており、現在は Python API の `tf.lite.TFLiteConverter` を用います。

### 2. どの処理がそのまま使えるか・捨てるべきか
- **捨てるべき処理 (そのまま使えないもの)**:
  - 古い `!pip install ... tf-estimator-nightly...`
  - TensorFlow リポジトリ全体の clone による `train.py`, `freeze.py` の借用
  - コマンドラインベースでの学習 (`!python ...train.py`) と `toco` オプション群
- **使える概念・再構成すべきもの**:
  - `wanted_words` = 対象ワード（今回は `yes` ではなく `"Hey ThinkPad"` に相当するもの）
  - 前処理ロジックの概念構造 (音声抽出 → スペクトログラム/MFCC → 2D CNN)
  - `tiny_conv` アーキテクチャ (軽量なモデルで Keyword Spotting を行う点)

### 3. 現行環境への置き換え方針
旧 Notebook（A案）を修理するとなると、レガシーな TF 1.x のコードを大量にフォーク・修正することになり、現行 Windows での動作や保守性に大きな支障が出ます。
したがって、**B案（現行 TensorFlow/Keras 系を利用して全く新しく再構築する）** を採用します。

具体的には以下のように移行します：
1. **データパイプライン**: `tf.data.Dataset` あるいは `librosa` / `scipy` を使って WAV ファイルを読み込み、短時間フーリエ変換 (STFT) や メルスペクトログラムを計算する前処理を Python (TF2) で素直に記述します。
2. **モデルアーキテクチャ**: `tf.keras.Sequential` を使って、軽量な 2D CNN (Conv2D -> MaxPooling -> Dense) を定義します。
3. **学習プロセス**: `model.compile` および `model.fit` でわかりやすく記述します。
4. **変換**: `tf.lite.TFLiteConverter.from_keras_model(model)` を使い、簡潔に TFLite (.tflite) モデルへ出力します。

### 4. 軽量化と "Hey ThinkPad" 対応
- **ウィンドウ幅とストライド**: 16kHz サンプリングとし、"Hey ThinkPad" の長さを考慮して音声長は **1.5秒** をベースとします（1秒だと "Hey ThinkPad" 全体が収まりきらない可能性があるため）。
- **特徴量**: MFCC または単純な log Mel Spectrogram を入力とし、モデル自体の計算量（MACs）を削減します。
- **推論アプリの親和性**: 複雑な `tf.signal` 処理を TFLite 内部に押し込むよりも、今回は Windows 上の CPU 推論を意図しているため、Python 側 (PyAudio 等 + librosa/numpy) で前処理を行い、TFLite モデルにはスペクトログラム (または MFCC) の 2D 配列を渡す構成が開発スピード・デバッグのしやすさの観点で優れています。
