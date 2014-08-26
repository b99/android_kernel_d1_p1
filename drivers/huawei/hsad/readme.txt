目录结构
	auto-generate:
		所有产品模块自动生成的*.c 文件,特别注意，里面不加其它不相关的文件，不然会影响到config_total_product.c的生成
	tools:
		boardid 所有的脚本文件 boardid.sh是入口脚本，供build_viva.sh使用
	viva、front
		产品的名字，里面是各模块的配置文件

自动生成的文件:
	config_total_product.c
	config_boardid.h
	auto-generate


目前实现:
boardid 配置在 产品名/hw_configs.xml <id id0="" id1="" id2=""/>


增加boarid的步骤。
	1.增加产品目录，如viva
	2.在产品目录里面 增加配置文件，可以参考其它产品
	3.修改hw_configs.xml里面id