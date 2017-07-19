#!/bin/sh
set -e

if [ $# = 0 ]; then	
  	echo -e "\033[40;33m"
		echo warming  your had not inputed assemble model
		echo "$PWD"
		echo autogen-tongxingpoints-man [MODEL NAME]
		echo
		echo	EXAMPLE:
		echo
		echo	autogen-tongxingpoints-man ["tongxingpoints|tongxingpoints-test|tongxingpoints-ptest"]
	echo -e "\033[40;37m"
		exit 1
elif [ $# = 1 ]; then
	case $1 in 
		tongxingpoints)
		flag1=--with-daemon
		;;
		tongxingpoints-test)
		flag1=--enable-tests
		;;
		tongxingpoints-ptest)
		flag1=--enable-ptests
		;;
		*)
		echo -e "\033[40;32m"
		echo warming:error para!
		echo -e "\033[40;37m"
		exit 1
		;;
	esac
elif [ $# = 2 ]; then
	case $1 in 
		tongxingpoints)
		flag1=--with-daemon
		;;
		tongxingpoints-test)
		flag1=--enable-tests
		;;
		tongxingpoints-ptest)
		flag1=--enable-ptests
		;;
		*)
		echo -e "\033[40;32m"
		echo warming:error para!
		echo -e "\033[40;37m"
		exit 1
		;;
	esac
	case $2 in 
		tongxingpoints)
		flag2=--with-daemon
		;;
		tongxingpoints-test)
		flag2=--enable-tests
		;;
		tongxingpoints-ptest)
		flag2=--enable-ptests
		;;
		*)
		echo -e "\033[40;32m"
		echo warming:error para!
		echo -e "\033[40;37m"
		exit 1
		;;
	esac
elif [ $# = 3 ]; then
	case $1 in 
		tongxingpoints)
		flag1=--with-daemon
		;;
		tongxingpoints-test)
		flag1=--enable-tests
		;;
		tongxingpoints-ptest)
		flag1=--enable-ptests
		;;
		*)
		echo -e "\033[40;32m"
		echo warming:error para!
		echo -e "\033[40;37m"
		exit 1
		;;
	esac
	case $2 in 
		tongxingpoints)
		flag2=--with-daemon
		;;
		tongxingpoints-test)
		flag2=--enable-tests
		;;
		tongxingpoints-ptest)
		flag2=--enable-ptests
		;;
		*)
		echo -e "\033[40;32m"
		echo warming:error para!
		echo -e "\033[40;37m"
		exit 1
		;;
	esac
	case $3 in 
		tongxingpoints)
		flag3=--with-daemon
		;;
		tongxingpoints-test)
		flag3=--enable-tests
		;;
		tongxingpoints-ptest)
		flag3=--enable-ptests
		;;
		*)
		echo -e "\033[40;32m"
		echo warming:error para!
		echo -e "\033[40;37m"
		exit 1
		;;
	esac
else
	echo -e "\033[40;32m"
	echo warming  your had inputed illegal params
   	echo please insure the params in [tongxingpoints|tongxingpoints-test|tongxingpoints-ptest]
	echo -e "\033[40;37m" 
	exit 1
fi

srcdir="$(dirname $0)"
cd "$srcdir"
autoreconf --install --force

CPPFLAGS="-std=c++11" \
./configure \
--disable-upnp-default \
--enable-debug \
--without-gui \
$flag1 \
$flag2 \
$flag3 \
$flag4 \
--with-protoc-bindir=/c/deps/protobuf-2.5.0/src \
--with-incompatible-bdb
