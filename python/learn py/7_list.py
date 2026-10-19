mainList = list((1, 2, 3, 4, 5))
print(mainList)
print("\n")

inLists = [1, '2', False, 4.68, 5e87j, None, [1, 2], (3, 4), {5: 'five'}, {6, 7}]
print(inLists)
print(type(inLists))
print("Number of elements:", len(inLists))

for list in inLists:
   print(list, type(list))
   print ("---------------------------")

print(inLists[1])  # Accessing the second element
print(inLists[-1])  # Accessing the last element
print(inLists[2:5])  # Slicing from index 2 to 4

if 1 in inLists:
   print("1 is in the list")

inLists[0] = 'One'
inLists[1:3] = ["pizza", "burger"]
inLists.insert(3, "sushi")
inLists.append("tacos")
print(inLists)

mainList.extend(inLists)
print(mainList)

inLists.remove(4.68)  # Removing the float 4.68
inLists.pop()  # Removing the last element
inLists.pop(0)  # Removing the first element
del inLists[2]  # Deleting the element at index 2
# inLists.clear()  # Removing all elements
print(inLists)

print ("\n")
#loop print list options

for i in range(len(inLists)):
   print (inLists[i])

print("\n")

i = 0
while i < len(inLists):
   print (inLists[i])
   i += 1

print("\n")

[print(i) for i in inLists]
