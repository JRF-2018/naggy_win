use strict; use warnings; use Unicode::Japanese;
binmode STDOUT, ":utf8";
for my $f (qw(z2h h2z hira2kata kata2hira)) {
  for my $c (0x20..0xFFFF) {
    next if $c >= 0xD800 && $c <= 0xDFFF;
    my $s = chr($c);
    my $u = $s; utf8::encode($u);
    my $r = Unicode::Japanese->new($u)->$f->getu;
    utf8::decode($r) unless utf8::is_utf8($r);
    print "$f\t", sprintf("%04X", $c), "\t", join(" ", map {sprintf "%04X", ord} split //, $r), "\n" if $r ne $s;
  }
}
# pairs: halfwidth kana + dakuten/handakuten, and fw kana + combining/spacing marks
for my $f (qw(h2z z2h hira2kata kata2hira)) {
  for my $a (0xFF61..0xFF9F, 0x3041..0x3096, 0x30A1..0x30FA) {
    for my $b (0xFF9E, 0xFF9F, 0x309B, 0x309C, 0x3099, 0x309A) {
      my $s = chr($a).chr($b);
      my $u = $s; utf8::encode($u);
      my $r = Unicode::Japanese->new($u)->$f->getu;
      utf8::decode($r) unless utf8::is_utf8($r);
      my $r1 = Unicode::Japanese->new(do{my $x=chr($a);utf8::encode($x);$x})->$f->getu; utf8::decode($r1) unless utf8::is_utf8($r1);
      my $r2 = Unicode::Japanese->new(do{my $x=chr($b);utf8::encode($x);$x})->$f->getu; utf8::decode($r2) unless utf8::is_utf8($r2);
      print "pair-$f\t", sprintf("%04X %04X", $a, $b), "\t", join(" ", map {sprintf "%04X", ord} split //, $r), "\n" if $r ne $r1.$r2;
    }
  }
}
