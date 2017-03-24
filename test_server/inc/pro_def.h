#pragma once

const unsigned char CHECK = 0xef;
struct pro_head {
	unsigned short len;
	unsigned char check;
};

