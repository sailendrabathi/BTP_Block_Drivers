import threading
import hashlib
import shutil

file = "/home/sailendra/sbull_test/test.txt"


def compute_md5(file_name):
    hash_md5 = hashlib.md5()
    with open(file_name, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

print("1.Writing known data offsets and checking.")

def simple_write():
	s = "I'm bulletproof, nothing to lose\n Fire away, Fire away"
	f = open(file, "w")
	f.writelines(s)
	f.close()

	f = open(file, "r")
	red = f.read()
	f.close()
	assert red==s
	print("Test passed")

def simple_offset():
	s = "I'm bulletproof, nothing to lose\n Fire away, Fire away"
	f = open(file, "w")
	f.seek(20)
	f.writelines(s)
	f.close()

	f = open(file, "r")
	f.seek(20)
	red = f.read()
	f.close()
	assert red==s
	print("Test passed")

def overlap():
	s = "I'm bulletproof, nothing to lose\n Fire away, Fire away"
	f = open(file, "w+")
	f.seek(20)
	f.writelines(s)
	f.close()

	# f = open(file, "r")
	# f.seek(20)
	# red = f.read()
	# f.close()
	# assert red==s
	# print("Test passed")

	f = open(file, "r+")
	f.seek(25)
	f.writelines("I am titanium")
	f.close()

	f = open(file, "r")
	print(f.read())
	f.close()


simple_write()
simple_offset()
overlap()

print("2.Testing non-sector aligned writes.")

print("3.Testing parallel IO with multiple processes.")

def task(offset, fl):
	s = "I'm bulletproof, nothing to lose\n Fire away, Fire away"
	f = open(fl, "w")
	f.seek(offset)
	f.writelines(s)
	f.close()

	f = open(fl, "r")
	f.seek(offset)
	red = f.read()
	f.close()
	assert red==s
	print("Test passed")


f1 = "/home/sailendra/sbull_test/test1.txt"
f2 = "/home/sailendra/sbull_test/test2.txt"

t1 = threading.Thread(target=task,  args=(50,f1), name="t1")
t2 = threading.Thread(target=task,  args=(100,f2), name="t2")

t1.start()
t2.start()

t1.join()
t2.join()


print("4.Stress testing single process and large IO.")

big_file_src = "512KB_file.jpg"
big_file_dest = "/home/sailendra/sbull_test/512KB_file.jpg"

shutil.copyfile(big_file_src,big_file_dest)

src_hash = compute_md5(big_file_src)
dest_hash = compute_md5(big_file_dest)

if(src_hash == dest_hash):
	print("Test passed")
else:
	print("Test failed")	

print("5.Stress testing multiple processes and large IO.")

big_file_dest1 = "/home/sailendra/sbull_test/512KB_file1.jpg"
big_file_dest2 = "/home/sailendra/sbull_test/512KB_file2.jpg"

t1 = threading.Thread(target=shutil.copyfile,  args=(big_file_src,big_file_dest1), name="t1")
t2 = threading.Thread(target=shutil.copyfile,  args=(big_file_src,big_file_dest2), name="t2")

t1.start()
t2.start()

t1.join()
t2.join()

src_hash = compute_md5(big_file_src)
dest_hash1 = compute_md5(big_file_dest1)
dest_hash2 = compute_md5(big_file_dest2)

if((src_hash == dest_hash1) and (src_hash == dest_hash2)):
	print("Test passed")
else:
	print("Test failed")

print("6.Testing boundary conditions. ???")
