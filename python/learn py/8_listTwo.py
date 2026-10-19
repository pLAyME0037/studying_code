strList = ["apple", "banana", "jackfruit", "cherry", "grape", "elderberry"]
#list comprehension to filter elements containing 'a'
list_a = [ele for ele in strList if 'a' in ele]
#condition filtering
list_b = [ele for ele in strList if ele != "banana"]
#iterable filtering with range
list_c = [ele for ele in range(10) if ele > 7]
#converting elements to uppercase
list_d = [ele.upper() for ele in strList]
#replacing 'banana' with 'pine apple'
list_e = [ele if ele != "banana" else "pine apple" for ele in strList]
# print(list_a)
# print (list_b)
# print (list_c)
# print (list_d)
print(list_e)

strList.sort()
strListCopy = strList.copy()
strListCopy.sort(reverse=True) # or strListCopy.reverse()
print("Sorted List:", strList)
print("Descending Order:", strListCopy)

#list concatenation
numList = [1, 2, 3, 4, 5]

comList = numList + strList

print (comList)
#other list methods
print("Length of comList:", len(comList))
print("Index of 'cherry':", comList.index("cherry"))
print("Count of 'apple':", comList.count("apple"))

