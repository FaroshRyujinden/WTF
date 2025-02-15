g++ -shared -fPIC -o Wtf.so Wtf.cpp $(pkg-config --cflags --libs opencv4) -lvulkan

g++ -shared -fPIC -o Hydra.so Hydra.cpp $(pkg-config --cflags --libs opencv4) -lvulkan

g++ -shared -fPIC -o Omg.so Omg.cpp $(pkg-config --cflags --libs opencv4) -lvulkan
