#!/usr/bin/perl -w

#History       :
########################################################################
# DTS ID:           Author:        Date:        Modification:
#-------------------------------------------------------------------------

########################################################################

use Cwd;




my $current_dir = getcwd();


opendir(CUR_DIR, $current_dir) || die "current directory not exist";
my @file_s = readdir(CUR_DIR);
my $subdir;
my $product;

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
				system("perl tools/parse_product.pl ".$subdir."/".$_." $product");

			}

		}
		closedir(SUB_DIR);

}

closedir(CUR_DIR);

#generate every product configs




