#pragma once

const unsigned char CHECK = 0xaf;
struct pro_head {
	unsigned short len;
	unsigned char check;
};

