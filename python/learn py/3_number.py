a = (1, -324.e89, 4+5e87j)
b, c, d = a
print(type(b), type(c), type(d))

e = complex(b)
f = int(c)
g = float(a[1])

print(e, type(e))
print(f, type(f))
print(g, type(g))

import random
print(random.randint(1, 10) + 2)