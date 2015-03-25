#!/bin/bash
#History       :
########################################################################
# DTS ID:           Author:        Date:        Modification:
#-------------------------------------------------------------------------
#
########################################################################

AUTO_GENERATE=auto-generate
PWD=`pwd`

#current dir kernel\drivers\huawei\hsad
cd ../

#mkdir auto-generate
if [ -d ${AUTO_GENERATE} ]
then :
else
	mkdir ${AUTO_GENERATE}
fi
#common module
echo "parse_product_id.pl begin"
perl tools/parse_product_id.pl
echo "parse_product_id.pl end"
#gpio module
echo "gen_gpio_cfg.pl begin"
perl tools/gen_gpio_cfg.pl
echo "gen_gpio_cfg.pl end"
if [ $? -ne 0 ];then
    exit 1
fi

#power tree
echo "parse_power_tree.pl begin"
perl tools/parse_power_tree.pl

#auto generated config_total_product.c
perl tools/total_product.pl

#auto generated boardid
perl tools/boardid_macro_product.pl
