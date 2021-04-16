import threading
import shutil

# file = "/home/varshitha/sbull_test/test.txt"

# file_path = "/home/varshitha/test.img"
file_path = "/dev/sbulla"
img_path0 = "/home/sailendra/loopbackfile0.img"
img_path1 = "/home/sailendra/loopbackfile1.img"
img_path2 = "/home/sailendra/loopbackfile2.img"

def bitwise_xor_bytes(a, b):
	result_int = int.from_bytes(a, byteorder="big") ^ int.from_bytes(b, byteorder="big")
	return result_int.to_bytes(max(len(a), len(b)), byteorder="big")


data = "The caged bird sings\nwith a fearful trill\nof things unknown\nbut longed for still"

f = open(file_path, 'wb+')
f.write(b'hello world')
f.close()

f = open(file_path, 'rb')
temp = f.read(5)
print(temp)
assert temp == b'hello'
f.close()

f = open(file_path, 'rb')
temp = f.read(11)
print(temp)
assert temp == b'hello world'
f.close() 

f = open(img_path0, 'rb')
temp = f.read(11)
# print(temp)
assert temp == b'hello world'
f.close()


f = open(file_path, 'rb')
f.seek(-5, 2)
temp = f.read(5)
print(temp)
assert temp == b'\00\00\00\00\00'
f.close()

f = open(file_path, 'rb+')
f.seek(511, 0)
f.write(b'hello world')
f.close()

f = open(file_path, 'rb')
f.seek(510, 0)
temp = f.read(2)
print(temp)
assert temp == b'\00h'
temp = f.read(10)
print(temp)
assert temp == b'ello world'
f.close()

f = open(file_path, 'rb')
f.seek(510, 0)
temp = f.read(13)
print(temp)
assert temp == b'\00hello world\00'
f.close()

f = open(img_path0, 'rb')
f.seek(510, 0)
temp = f.read(13)
# print(temp)
assert temp == b'\00hello world\00'
f.close()

f = open(file_path, 'rb+')
f.seek(4096, 0)
f.write(b'hello world')
f.close()

f = open(file_path, 'rb')
f.seek(4097, 0)
temp = f.read(5)
print(temp)
assert temp == b'ello '
f.close()

f = open(img_path1, 'rb')
f.seek(1, 0)
temp = f.read(5)
# print(temp)
assert temp == b'ello '
f.close()

f = open(img_path1, 'rb')
f.seek(1, 0)
temp = f.read(10)
# print(temp)
assert temp == b'ello world'
f.close()

# f = open(img_path2, 'rb')
# temp = f.read(11)
# assert temp == b'\00\00\00\00\00\00\00\00\00\00\00'
# f.close()

# f = open(img_path2, 'rb')
# f.seek(511, 0)
# temp = f.read(11)
# # print(temp)
# assert temp == bitwise_xor_bytes(b'hello world', b'\00')
# f.close()

# f = open(file_path, 'rb+')
# f.seek(4607, 0)
# f.write(b'hi mate')
# f.close()

# f = open(img_path2, 'rb')
# f.seek(511, 0)
# temp = f.read(11)
# # print(temp)
# assert temp == bitwise_xor_bytes(b'hello world', b'hi mate\00\00\00\00')
# f.close()

def big_io(file_path, offset):
	f = open(file_path, 'rb+')
	for it in range(5):
		print("bio IO itr", it, "start", offset)
		f.seek(offset, 0)
		for i in range(512):
			data = ((i%256).to_bytes(1, 'big'))*1024
			f.write(data)
	f.close()


print("Multi-threading")

# big_file_dest1 = "/home/varshitha/sbull_test/512KB_file1.jpg"
# big_file_dest2 = "/home/varshitha/sbull_test/512KB_file2.jpg"

offset1 = 0
offset2 = 512*1024 + 4096

t1 = threading.Thread(target=big_io,  args=(file_path, offset1), name="t1")
t2 = threading.Thread(target=big_io,  args=(file_path, offset2), name="t2")

t1.start()
t2.start()

t1.join()
t2.join()

f = open(img_path1, 'rb')
temp = f.read(1024)
assert temp == b'\x04'*1024
f.close()

f = open(img_path1, 'rb')
f.seek(4096, 0)
temp = f.read(1024)
assert temp == b'\x0C'*1024
f.close()

f = open(img_path2, 'rb')
f.seek(4096*20 + 1024, 0)
temp = f.read(1024)
assert temp == b'\xa5'*1024
f.close()

f = open(img_path1, 'rb')
f.seek(218*1024+4096, 0)
temp = f.read(1024)
assert temp == b'\x04'*1024
f.close()

f = open(img_path2, 'rb')
f.seek(218*1024+20*4096+1024, 0)
temp = f.read(1024)
assert temp == b'\x1C'*1024
f.close()

f = open(file_path, 'rb+')
f.seek(-11, 2)
f.write(b'hello world')
f.close()

f = open(img_path1, 'rb')
f.seek(6400*4096-11, 0)
temp = f.read(11)
assert temp == b'hello world'
f.close()

print("Tests Done")
