#!/bin/sh
# git clone した状態から Eclipse 介さず一通りビルドする
# NiosII Command Shell で、カレントディレクトリで実行する

sopc_builder --generate mb_de0nano_sopc 

if grep -q "Info: System generation was successful." sopc_builder_log.txt; then
	echo ""
else
	echo ""
	echo "SOPC Builder failed"
	exit
fi

rm -f onchip_memory2_0.hex

cd software/mb_de0nano_c_bsp/
nios2-bsp-generate-files --bsp-dir . --settings settings.bsp || exit

cd ../mb_de0nano_c_demo
make mem_init_install || exit

cd ../..
if [ ! -f onchip_memory2_0.hex ] ; then
	echo ""
	echo "generate onchip_memory2_0.hex failed"
	exit
fi
quartus_sh --flow compile mb_de0nano

