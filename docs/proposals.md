# jrf_tankanji-2026 と quail-naggy への提案

<!-- Time-stamp: "2026-11-25T13:00:00Z" -->

naggy_win の作成中に気づいた点です。いずれも必須の変更ではありません。

## jrf_tankanji-2026

### 1. 変更は必須ではない

naggy_win (Go 版 naggy-backend) は `tankanji-2026.txt` を euc-jisx0213 の
まま、インデックスファイルなしで読めます。形式の変更は不要です。

確認したこと:

* `#YOMI` ブロックは 7433 個、最大 3448 バイト (Perl 版
  `make_tankanji_dic_db.pl` の `pack("Vv")` の 64KB 制限内)。
* 読みの重複 (正規化後) はなし。
* 私用領域の文字はなし。BMP 外の文字は 301 種。

### 2. Perl 版で「�」になっているマスがある (情報)

Perl (5.40 で確認) の `Encode` の `euc-jp` は JIS X 0213 の大部分を読めます
が、次は U+FFFD になります。

* Unicode で 2 コードポイントになる合成文字 25 字 (`か゚` 1-4-87、`˩˥`
  1-11-69 など)
* JIS X 0213:2004 で追加された 10 字 (1-14-1, 1-15-94, 1-47-52, 1-47-94,
  1-84-7, 1-94-90〜94)

`tankanji-2026.txt` ではこれらが 184 マスあり、quail-naggy (Perl 版) では
「�」と表示されているはずです。Go 版は EUC-JIS-2004 として正しく読み、合成
文字も 1 マスとして扱います。

もしこれらの字が意図したものでなければ削るか、Perl 版利用者向けに README で
`Encode::JIS2K` (`euc-jis-2004`) の利用を案内するとよいかもしれません。

### 3. UTF-8 版を作る場合の注意

単漢字辞書の形式は「1 マス = 1 文字 (または ASCII 2 文字の空白)」です。
UTF-8 にすると上の合成文字が 2 コードポイントになり、また単独の結合文字
(1-11-xx のアクセント記号など) もあるため、マスの区切りが曖昧になります。
UTF-8 版を出すなら、

* 合成文字はマスの後ろに区切りを入れる、あるいは
* 「結合文字は直前の文字と同じマス」という規則を決める (ただし単独の結合
  文字がある現状とは両立しない)

などの規則が必要です。現状の euc-jisx0213 のままが無難だと思います。

### 4. リリースとバージョン表記

naggy_win のインストーラは現在
`https://raw.githubusercontent.com/JRF-2018/jrf_tankanji-2026/master/tankanji-2026.txt`
をダウンロードしています。

* GitHub の Release (タグ) に `tankanji-2026.txt` を添付してもらえると、イン
  ストーラが特定の版を取得でき、再現性が上がります。
* ファイル先頭に `# tankanji-2026 2026-05-31` のようなコメント行を入れると、
  インストーラで版を表示したり、更新の有無を判定できます (`#YOMI` 以外の
  `#` 行は無視されるので互換性は保たれます)。

### 5. ライセンスの明示と同梱の可否

README では「パブリックドメインを望むが、使いにくければ MIT License で」と
あり、『風』の購入のお願いがあります。

* `LICENSE` ファイル (例えば MIT か CC0) を置き、『風』購入のお願いは README
  の「お願い」として残すと、再配布の条件がはっきりします。
* naggy_win のインストーラに `tankanji-2026.txt` を同梱してよいか(オフライン
  インストールのため)を決めていただければ、そのように対応できます。現在は同
  梱せずダウンロードしています。

## quail-naggy

1. `nginit/default-init.nginit` の `CAPITALIZED_TANKANJI` の判定に
   `|| ${FRONT_END:e} == naggy_win` を加える (naggy_win 付属版はそうしてい
   ます)。Perl 版を naggy_win から使うときに `FRONT_END=quail-naggy` を指定
   しなくて済みます。
2. `00README.utf8.txt` の「Encode 1.64 から euc-jp で euc-jisx0213 の内容を
   デコードできる」は、上の 2. のとおり一部の字 (合成文字と 2004 年追加字)
   については成り立ちません。
3. Go 版 naggy-backend を同梱すれば、Perl・Unicode::Japanese・SDBM インデッ
   クス作成なしで使えるようになります (`naggy-backend-options` を nil に)。
   Dockerfile の手順も簡単になります。
4. Perl 版の小さなバグ (Go 版では修正済み):
   * `Naggy::Backend::Translit::rename` の `_rename_in_array` が `map` の中
     で `return` しているため、配列の最初の要素しか残らない。
   * `read-with-encoding-e` (unsafe) で `open` の成否の判定が逆。
   * `convert` の Unicode 変換で `if (! $s =~ /.../)` の優先順位のため、不正
     な 16 進数の警告が出ない。
