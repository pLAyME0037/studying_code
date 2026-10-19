a = 1j
print (a)
print (type(a))

b = range(5)
print (b, type(b))

c = frozenset({"apple", "banana", "pineapple"})
print (c, type(c))

d = b'hello'
print (d, type(d))

e = bytearray(7)
print (e, type(e))

f = memoryview(bytes("hello world", "utf-8"))
print (f, type(f))

g = None
print (g, type(g))