#!/usr/bin/perl -w

#History       :
########################################################################
# DTS ID:           Author:        Date:        Modification:
#--------------------------------------------------------------------------------------
########################################################################
use Cwd;
sub config_total_product()
{

	my $current_dir = getcwd();
    opendir(CUR_DIR, $current_dir) || die "current directory not exist";
    my @file_s = readdir(CUR_DIR);
    my $config_file;
    my $subdir;
	my $dir;
	my $handle_out_file;

	open($handle_out_file, ">config_total_product.c") || die("Failed to open file config_total_product for write!");

	my @file_sub;
	my @sort_sub;
	   foreach(@file_s){
			#ignore tools/auto-generate/./.. dir
			if(($_ =~ m/auto-generate/))
			{

				$subdir = "$current_dir/$_";
				$dir = $_;
				opendir(SUB_DIR, $subdir) || die("Failed to open dir auto-generate for write!");
				@file_sub = readdir(SUB_DIR);
				@sort_sub = sort @file_sub;
				print $handle_out_file "/*Copyright Huawei Technologies Co., Ltd. 1998-2011. All rights reserved. \n";
				print $handle_out_file " *This file is Auto Generated */\n";
				print $handle_out_file "\n\n";
				foreach(@sort_sub)
				{

					if(($_ =~ m/\.c/)||($_ =~ m/\.h/))
					{


						print $handle_out_file "#include \"$dir/$_\"\n";


					}

				}
				print $handle_out_file "\n\n";
				print $handle_out_file "struct board_id_general_struct  *hw_ver_total_configs[] = \n";
				print $handle_out_file "{\n";
				foreach(@sort_sub)
				{

					if(($_ =~ m/\.c/)||($_ =~ m/\.h/))
					{
						$config_file = $_;
						$config_file =~ s/\.c$//;
						$config_file =~ s/\.h$//;


						print $handle_out_file "	&$config_file,\n";


					}

				}
				print $handle_out_file "};\n";
				closedir(SUB_DIR);
			}

		}
	close($handle_out_file);
	closedir(CUR_DIR);
}
&config_total_product();
