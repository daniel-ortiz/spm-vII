

char* print_access_type(int entry)
{
	char* out;
	size_t sz = 500 - 1; /* -1 for null termination */
	size_t i, l = 0;
	unsigned int m =  PERF_MEM_LVL_NA;
	unsigned int hit, miss;
	out=malloc(500*sizeof(char));

	m  = (unsigned int)entry;

	out[0] = '\0';

	hit = m & PERF_MEM_LVL_HIT;
	miss = m & PERF_MEM_LVL_MISS;

	/* already taken care of */
	m &= ~(PERF_MEM_LVL_HIT|PERF_MEM_LVL_MISS);

	for (i = 0; m && i < NUM_MEM_LVL; i++, m >>= 1) {
		if (!(m & 0x1))
			continue;
		if (l) {
			strcat(out, " or ");
			l += 4;
		}
		strncat(out, mem_lvl[i], sz - l);
		l += strlen(mem_lvl[i]);
	}
	if (*out == '\0')
		strcpy(out, "N/A");
	if (hit)
		strncat(out, " hit", sz - l);
	if (miss)
		strncat(out, " miss", sz - l);

	return  out;
}
