num = 0

for i in range(100000000):
    num += 1 % 7
    print("Num", i, ": ", num)

