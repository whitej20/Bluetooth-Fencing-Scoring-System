import socket

listenPort = 6969

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
sock.bind(("", listenPort))

print("Listening for broadcast packets...")

while True:
    data, addr = sock.recvfrom(1024)
    print("Received:", data.decode("utf-8"), "from", addr)
