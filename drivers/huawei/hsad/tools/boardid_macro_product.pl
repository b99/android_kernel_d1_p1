#!/usr/bin/perl -w

#History       :
########################################################################
# DTS ID:           Author:        Date:        Modification:
#-------------------------------------------------------------------------
#
########################################################################

use Cwd;




my $current_dir = getcwd();


my $subdir;
my $product;
my $key; #store the keys we already meet.
my $handle_out_file;
opendir(CUR_DIR, $current_dir) || die "current directory not exist";
open($handle_out_file, ">../../../include/hsad/config_boardid.h") || die("Failed to open file config_total_product for write!");
my @file_s = readdir(CUR_DIR);
print $handle_out_file "/*Copyright Huawei Technologies Co., Ltd. 1998-2011. All rights reserved. \n";
print $handle_out_file " *This file is Auto Generated */\n";
print $handle_out_file "\n\n";
print $handle_out_file "#ifndef _CONFIG_BOARDID_H\n";
print $handle_out_file "#define _CONFIG_BOARDID_H\n";
print $handle_out_file "\n\n";
foreach(@file_s)
{

		#ignore tools/auto-generate/./.. dir
		if(($_ =~ m/tools/)||($_ =~ m/auto-generate/)||($_ =~ m/\./)||($_ =~ m/\.\./))
		{
			next;
		}


		#open sub dir
		$subdir = "$current_dir/$_";
		$product = $_;
		opendir(SUB_DIR, $subdir) || next;
		@file_sub = readdir(SUB_DIR);
		foreach(@file_sub)
		{

			if($_ =~ m/configs\.xml$/)
			{
				open FXML, "<$subdir/$_" || die "can't open $ARGV[0]! please check your command and filesystem.";
				while(<FXML>)
				{

					if (/^\s*<id\s+id0\s*=\s*\"(\w+)\"\s+id1\s*=\s*\"(\w+)\"\s+id2\s*=\s*\"(\w+)\"\s*\/>\s*$/i)
					{
						$key = "0x".$1.$2.$3;
						print $handle_out_file "#define BOARD_ID_".uc($product)." $key\n";
					}

				}
				close FXML;
			}

		}
		closedir(SUB_DIR);

}

print $handle_out_file "\n\n#define GPIO_MODULE_NAME  \"gpio\"\n";
print $handle_out_file "#define COMMON_MODULE_NAME  \"common\"\n";
print $handle_out_file "#define POWER_TREE_MODULE_NAME  \"power_tree\"\n\n";
print $handle_out_file "#endif\n";




close($handle_out_file);
closedir(CUR_DIR);






