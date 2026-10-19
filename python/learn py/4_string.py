#multiline string
print ("""hr eidhf giuh
dfh fiuhrrr rrr
sdfhuvfr eu
gvjer oi thyguv""")

#array str
a = 'Hello,', 'world', '.py'
print (a[0], a[2], a[1])
print()
#loop through string
for i in a:
    print (i)
print()
#string length
print (len(a[1]), "\n")

#check string
txt = "Hello, World!"
word = 'World'
stmt = False
if word in txt: #if word not in txt:
   stmt = True

if stmt == True:
   print(word, "is in the string.")
else:
   print(word, "is not in the string.")

#slicing
print(txt[5:12])
print(txt[:5])
print(txt[7:])
print(txt[-6:-1])

#to uppercase and lowercase
print(txt.upper())
print(txt.lower())
#remove whitespace
settxt = "   Hello, World!   "
print(settxt.strip())  # removes any whitespace at the beginning or the end
#replace string
print(txt.replace("World", "Python"))
#spilt str
t = txt.split(",")  # returns a list
print(t)

#concat strings
x = "Hello"
y = "World"
z = x + ", " + y + "!"
print(z + "\n")

#formatted f-string
age = 20
name = "mean"
print(f"{name} is {age} years old.\n")

price = 2
print (f"The price is {price:.2f} dollars.")
print (f"The price is {price * 2:6.2f} dollars.")
print (f"The price is {price:06.2f} dollars.")
print(f"The price is {price:0>6.2f} dollars.\n")

#escaping characters
print("He said, \"Hello, World!\"")
print("He said, \\Hello, World!")
print("He said, \nHello, World!")
print("He said, \tHello, World!")
print("He said, \rHello, World!")
print("He said, \bHello, World!")
print("He said, \fHello, World!")
print("He said, \vHello, World!")
print("He said, \110\145\154\154\157, World!")
print("He said, \x27Hello, World!")
print("He said, \u0048\u0065\u006C\u006C\u006F, World!\n")

#all string methods
print("Hello, World!".capitalize())
print("Hello, World!".casefold())
print("Hello, World!".center(20))
print("Hello, World!".count("o"))
print("Hello, World!".encode())
print("Hello, World!".endswith("!"))
print("Hello, World!".find("World"))
print("Hello, World!".index("World"))
print("Hello, World!".isalnum())
print("Hello, World!".isalpha())
print("Hello, World!".isnumeric())
print("Hello, World!".join(["Hello", "World"]))
print("Hello, World!".lstrip())
print("Hello, World!".rstrip())
print("Hello, World!".strip())
print("Hello, World!".swapcase())
print("Hello, World!".title())
print("Hello, World!".zfill(20))
print("Hello, World!".isupper())
print("Hello, World!".islower())
print("Hello, World!".isprintable())
print("Hello, World!".split())
print("Hello, World!".startswith("Hello"))
print("Hello, World!".translate(str.maketrans("HW", "hw")))
print("Hello, World!".removeprefix("Hello, "))
print("Hello, World!".removesuffix(" World!"))
print("Hello, World!".partition("World"))
print("Hello, World!".rpartition("World"))
print("Hello, World!".replace("World", "Python"))
print("Hello, World!".rfind("World"))
print("Hello, World!".rindex("World"))
print("Hello, World!".rjust(20))
print("Hello, World!".ljust(20))
print("Hello, World!".format_map({"name": "mean", "age": 20}))
