file = "/home/sailendra/sbull_test/test.txt"


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