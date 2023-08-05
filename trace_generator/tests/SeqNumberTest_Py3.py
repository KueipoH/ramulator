import sys

def main():
    oldno = -1
    old_write = False
    lineno = 0
    error = 0

    if len(sys.argv) < 3:
        print("This test checks if the sequence numbers are correctly recorded.")
        print("USAGE: python SeqNumberTest.py <dependency trace file> <window size : if not changed give 128>")
        sys.exit(1)

    with open(sys.argv[1]) as f:
        for line in f:
            tokens = line.split(" ")

            if tokens[0] == "#":
                lineno += 1
                continue

            sequence_number = int(tokens[0])
            window_size = int(sys.argv[2])

            if oldno == window_size - 1 and sequence_number != 0:
                if old_write and tokens[1] == "READ":
                    lineno += 1
                    old_write = False
                    continue
                print(f"Error in line no : {lineno} expected : 0 found : {sequence_number}")
                error += 1

            if oldno != window_size - 1 and oldno + 1 != sequence_number:
                if old_write and tokens[1] == "READ":
                    lineno += 1
                    old_write = False
                    continue
                print(f"Error in line no : {lineno} expected : {oldno + 1} found : {sequence_number}")
                error += 1

            lineno += 1

            if oldno != window_size - 1:
                oldno += 1
            else:
                oldno = 0

            if tokens[1] == "WRITE":
                old_write = True
            else:
                old_write = False

    if error > 0:
        print(f"FAILED error: {error}")
    else:
        print("SUCCESS")

if __name__ == '__main__':
    main()
