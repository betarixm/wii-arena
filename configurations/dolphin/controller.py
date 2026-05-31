# type: ignore
import os
import socket
import struct

from dolphin import controller, event

AGENT_STRUCT = "<H6f"
AGENT_SIZE = struct.calcsize(AGENT_STRUCT)

# Make sure to initialize dolphin before socket listening
await event.frameadvance()

server_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server_sock_path = os.getenv("CONTROL_SOCKET", "/tmp/dolphin_control.sock")

server_sock.bind(server_sock_path)
os.chmod(server_sock_path, 0o666)
server_sock.listen(1)
conn, _ = server_sock.accept()

while True:
    data = conn.recv(1024)

    if data.startswith(b"E"):
        payload = data[1:]
        num_agents_in_packet = payload[0]
        agent_data = payload[1:]
        for count in range(num_agents_in_packet):
            offset = count * AGENT_SIZE

            packed_header, sx, sy, csx, csy, tl, tr = struct.unpack_from(
                AGENT_STRUCT, agent_data, offset
            )

            agent_index = (packed_header >> 12) & 0b11
            btn_mask = packed_header & 0xFFF

            inputs = {
                btn: bool(btn_mask & (1 << j))
                for j, btn in enumerate(
                    [
                        "A",
                        "B",
                        "X",
                        "Y",
                        "Z",
                        "Start",
                        "Up",
                        "Down",
                        "Left",
                        "Right",
                        "L",
                        "R",
                    ]
                )
            }
            inputs.update(
                {
                    "StickX": sx,
                    "StickY": sy,
                    "CStickX": csx,
                    "CStickY": csy,
                    "TriggerLeft": tl,
                    "TriggerRight": tr,
                }
            )

            controller.set_gc_buttons(agent_index, inputs)

        await event.frameadvance()
        conn.sendall(b"D")

    elif data.startswith(b"Q"):
        conn.close()
        break
