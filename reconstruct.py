CHUNK_IN_SECTORS = 8
NUM_DISKS = 3
SECTOR_SIZE = 512
NSECTORS = 102400

NSTRIPES = int(NSECTORS/((NUM_DISKS-1)*CHUNK_IN_SECTORS))

paths = ["/home/sailendra/loopbackfile0.img", "/home/sailendra/loopbackfile1.img", "/home/sailendra/loopbackfile_2.img"]

disk_create = 2


CHUNK_BYTES = CHUNK_IN_SECTORS*SECTOR_SIZE

print(CHUNK_BYTES)

file_p = []
for d,path in enumerate(paths):
	if(d==disk_create):
		file_p.append((d,open(path, 'wb+')))
	else:
		file_p.append((d,open(path, 'rb')))	

def bitwise_xor_bytes(a, b):
	result_int = int.from_bytes(a, byteorder="big") ^ int.from_bytes(b, byteorder="big")
	return result_int.to_bytes(max(len(a), len(b)), byteorder="big")


for stripe_index in range(NSTRIPES):
	parity_disk = (NUM_DISKS-1) - ((stripe_index/4)%NUM_DISKS)
	
	data = b'\00' * CHUNK_BYTES
	for d,fp in file_p:
		if d == disk_create:
			continue
		
		temp = fp.read(CHUNK_BYTES)
		data = bitwise_xor_bytes(data, temp)

	file_p[disk_create][1].write(data)

for _,fp in file_p:
	fp.close() 

