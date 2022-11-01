all: mtk_dump

mtk_dump: mtk_dump.c mtk_cmd.h
	$(CC) -s -O2 -Wall -Wextra -Wno-unused -o $@ $^
