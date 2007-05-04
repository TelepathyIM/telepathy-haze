aclocal || exit
autoconf || exit
automake --add-missing --copy || exit
./configure $@
