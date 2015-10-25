
request: 
	g++ -Wall -g3 -O0 tests/main.cpp -o request -I. -lcurl -lcrypto
	@echo "done\n\nusage: ./request <url>"
	@echo "e.g.:"
	@echo ./request "http://119.254.1.6/fkwebserver/"
	@echo ./request "http://www.baidu.com"

clean:
	\rm -f ./request
