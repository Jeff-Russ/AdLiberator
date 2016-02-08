lvls = [.75, 1.5, 3, 6, 12, 24]

def get_db(i):
	db = 0.0
	for d in lvls:
		if i & 0x1:
			db += d
		i >>= 1
	return db

for i in range(2**6):
	db = get_db(i)
	print '"%.2f dB",' % db,
