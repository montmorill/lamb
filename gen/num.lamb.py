names = "zero one two three four five six seven eight nine ten".split()

for i, s in enumerate(names):
    if i == 0:
        print("zero f x = x;")
    else:
        print(f"{s} = succ {names[i-1]};")
