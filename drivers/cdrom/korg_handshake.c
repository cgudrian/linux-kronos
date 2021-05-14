unsigned int sense_digit(unsigned short d)
{
	return (d & 0x1c0) << 7 | (int)(d & 0xe000) >> 3 | (d & 7) << 7 | (d & 0x1e00) << 6 | (int)(d & 0x38) >> 3;
}

unsigned int sense_word(unsigned int w)
{
    return (w & 0x3f800) << 9 | (w & 0x7fc0000) >> 7 | w << 0x1b | (w & 0xf8000000) >> 0x15 | (w & 0x7e0) >> 5;
}
