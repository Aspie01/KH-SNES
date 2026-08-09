Things that must not compile.

§M0 asked for "a compile-fail check, documented not automated", and documented
is where it stayed.  A type-safety property that nothing exercises is a property
that has already stopped holding by the time anybody notices -- which is the
same failure this project keeps finding in other shapes: a table with no
consumer, a divergence that cannot fire, a routine with no caller.

One file per rejection.  `make -f Makefile.host typecheck` compiles each with
`-fsyntax-only` and requires the compiler to REFUSE it.  A file that starts
compiling is a hole in the type system that used to be closed.
