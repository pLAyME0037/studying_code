#boolen false value
bool(False)
bool("")
bool(0)
bool(None)
bool(())
bool([])
bool({})

class myFirstBool():
    def __len__(self):
        return 0

myobj = myFirstBool()
print(bool(myobj))  # This will print False since __len__ returns 0

def myFunc():
    return False

if myFunc():
    print("YES!")
else:
    print("NO!")

num = 0
print(isinstance(num, bool))  # This will print False since num is an int, not a bool
print(isinstance(num, int))   # This will print True since num is an int
print(isinstance(False, bool))  # This will print True since False is a bool
print(isinstance(True, bool))   # This will print True since True is a bool
