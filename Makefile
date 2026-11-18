CXX=g++
CXXFLAGS=-std=c++17 -Wall -Wextra -O2
LDFLAGS=-pthread
TARGET=target

all: $(TARGET)/discovery_server $(TARGET)/chat_server_thread $(TARGET)/chat_server_select $(TARGET)/chat_server_fork $(TARGET)/chat_client

$(TARGET):
	@mkdir -p $(TARGET) 2>/dev/null || true

$(TARGET)/discovery_server: discovery_server.cpp common.hpp | $(TARGET)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(TARGET)/chat_server_thread: chat_server_thread.cpp common.hpp | $(TARGET)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(TARGET)/chat_server_select: chat_server_select.cpp common.hpp | $(TARGET)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(TARGET)/chat_server_fork: chat_server_fork.cpp common.hpp | $(TARGET)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(TARGET)/chat_client: chat_client.cpp common.hpp | $(TARGET)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -rf $(TARGET)
