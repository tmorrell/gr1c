ENV: r1 r2;
SYS: g1 g2;

ENVINIT: !r1 & !r2;
ENVTRANS:
  [](((r1 & !g1) | (!r1 & g1)) -> ((r1' & r1) | (!r1' & !r1)))
& [](((r2 & !g2) | (!r2 & g2)) -> ((r2' & r2) | (!r2' & !r2)));
ENVGOAL:
  []<>!(r1 & g1)
& []<>!(r2 & g2);

SYSINIT: !g1 & !g2;
SYSTRANS: [](!g1' | !g2')
& [](((r1 & g1) | (!r1 & !g1)) -> ((g1 & g1') | (!g1 & !g1')))
& [](((r2 & g2) | (!r2 & !g2)) -> ((g2 & g2') | (!g2 & !g2')));
SYSGOAL:
  []<>((r1 & g1) | (!r1 & !g1))
& []<>((r2 & g2) | (!r2 & !g2));
