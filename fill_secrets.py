with open('secrets.txt') as input: 
    with open("src/secrets.h", "w") as output:
        for line_number, define in enumerate(input):
            output.write("#define {}".format(define))
