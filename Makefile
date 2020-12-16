cc=g++
out=meltpoc
objs=nt8.cpp
$(out): nt8.cpp
	$(cc) $(objs) -o $(out)

clean:
	rm $(out)