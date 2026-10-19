import sys
print(sys.version)
#syntax
if 5 > 2:
   print ("five is greater than two.")
if 2 < 5:
   print ("two is not greater than five.", end="\n\n")
# #Variable assignment
a = 5
b = "Hello, World!"

print (a)
print (b)
print ("\n")
# #casting
c = int(5)
d = float(5.0)
e = str(5)

print (c, "is an integer")
print (d, "is a float")
print (e, "is a string")
print ("\n")
# #multiple variables
f, g, h = 5, "Hello, World!", 5.0

print (f, "is an integer")
print (g, "is a string")
print (h, "is a float")
print ("\n")
# #multiple assignment
i = j = k = 5

print (i, "is an integer")
print (j, "is an integer")
print (k, "is an integer")
print ("\n")

#type
print (type(f), type(g), type(h))
print ("\n")
#tuple unpacking
l = (5, "Hello, World!", 5.0)
m, n, o = l
print (type(l))

print (m, "is an integer")
print (n, "is a string")
print (o, "is a float")
print ("\n")
#list unpacking + concatenation
p = ["[ ", "Hello, World!", " ]"]
q, r, s = p

print (q + r + s)
print (type(p))
print ("\n")
#dictionary unpacking
u = {"name" : "mean", "age" : 20}
v, w = u["name"], u["age"]
print (v, w)
print (type(u))
print ("\n")
# #set unpacking
x = {"Hello", "World", "Python"}
y, z, aa = x
print (y, z, aa)
print (type(x))
print ("\n")

#global variables
t = "python"

def tt ():
   global t
   t = "java"
   print (t, "(a variable inside a function)")
tt()
print (t, "(a variable outside of a function)")
