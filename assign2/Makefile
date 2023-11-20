target := llvmassignment
srcs := LLVMAssignment.cpp
testfile=testfile.c

.PHONY: all run clean test submit

all: $(target)

$(target): CMakeLists.txt $(srcs)
	@bash scripts/build.sh

run: $(target)
	@bash scripts/run.sh $(testfile)

testall: $(target)
	@bash scripts/testall.sh

clean:
	@rm $(target)
	@rm -r build

submit: CMakeLists.txt $(srcs)
	@tar czvf LLVMAssignment.tar.gz $^
