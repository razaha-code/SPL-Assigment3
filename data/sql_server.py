#!/usr/bin/env python3
"""
Basic Python Server for STOMP Assignment – Stage 3.3
Updated to match Java Database.java schema requirements.
"""

import socket
import sys
import threading
import sqlite3
import os

SERVER_NAME = "STOMP_PYTHON_SQL_SERVER"  # DO NOT CHANGE!
DB_FILE = "stomp_server.db"              # DO NOT CHANGE!

def recv_null_terminated(sock: socket.socket) -> str:
    """
    Helper function to read data until \0 is found.
    """
    data = b""
    while True:
        chunk = sock.recv(1024)
        if not chunk:
            return ""
        data += chunk
        if b"\0" in data:
            msg, _ = data.split(b"\0", 1)
            return msg.decode("utf-8", errors="replace")

def init_database():
    """
    Initializes the database tables if they do not exist.
    Reflected to match the schema used in Java's Database.java
    """
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()

    # 1. users Table (Lowercase to match Java)
    # Java inserts: username, password, registration_date
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS users (
            username TEXT PRIMARY KEY,
            password TEXT NOT NULL,
            registration_date TEXT
        )
    """)

    # 2. login_history Table (Matches Java)
    # Java inserts: username, login_time
    # Java updates: logout_time
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS login_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT NOT NULL,
            login_time TEXT NOT NULL,
            logout_time TEXT,
            FOREIGN KEY(username) REFERENCES users(username)
        )
    """)

    # 3. file_tracking Table (Matches Java)
    # Java inserts: username, filename, upload_time, game_channel
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS file_tracking (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            filename TEXT NOT NULL,
            username TEXT NOT NULL,
            upload_time TEXT,
            game_channel TEXT,
            FOREIGN KEY(username) REFERENCES users(username)
        )
    """)

    conn.commit()
    conn.close()
    print(f"[{SERVER_NAME}] Database initialized successfully.")


def execute_sql_command(sql_command: str) -> str:
    """
    Executes INSERT, UPDATE, DELETE commands.
    Returns 'SUCCESS' on success or 'ERROR:...' on failure.
    """
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    try:
        cursor.execute(sql_command)
        conn.commit()
        # Java expects "SUCCESS" (or simply no error)
        return "SUCCESS" 
    except Exception as e:
        return f"ERROR: {str(e)}"
    finally:
        conn.close()


def execute_sql_query(sql_query: str) -> str:
    """
    Executes SELECT queries.
    Returns results separated by '|' so Java can split them.
    Format: SUCCESS|row1|row2|row3...
    """
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    try:
        cursor.execute(sql_query)
        rows = cursor.fetchall()
        
        # 1. Start with SUCCESS
        result_parts = ["SUCCESS"]
        
        # 2. Add each row as a string representation
        for row in rows:
            result_parts.append(str(row))
            
        # 3. Join everything with the pipe character '|'
        return "|".join(result_parts)
        
    except Exception as e:
        return f"ERROR: {str(e)}"
    finally:
        conn.close()


def handle_client(client_socket: socket.socket, addr):
    print(f"[{SERVER_NAME}] Client connected from {addr}")

    try:
        while True:
            message = recv_null_terminated(client_socket)
            if message == "":
                break

            print(f"[{SERVER_NAME}] Received SQL: {message}")

            # Decide if it's a Query (SELECT) or Command (INSERT/UPDATE/DELETE)
            # Simple heuristic: Check if it starts with SELECT (case insensitive)
            response = ""
            clean_msg = message.strip().upper()
            
            if clean_msg.startswith("SELECT"):
                response = execute_sql_query(message)
            else:
                response = execute_sql_command(message)

            # Send response followed by null character
            client_socket.sendall((response + "\0").encode("utf-8"))

    except Exception as e:
        print(f"[{SERVER_NAME}] Error handling client {addr}: {e}")
    finally:
        try:
            client_socket.close()
        except Exception:
            pass
        print(f"[{SERVER_NAME}] Client {addr} disconnected")


def start_server(host="127.0.0.1", port=7778):
    # Initialize DB before starting to listen
    init_database()

    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    try:
        server_socket.bind((host, port))
        server_socket.listen(5)
        print(f"[{SERVER_NAME}] Server started on {host}:{port}")
        print(f"[{SERVER_NAME}] Waiting for connections...")

        while True:
            client_socket, addr = server_socket.accept()
            t = threading.Thread(
                target=handle_client,
                args=(client_socket, addr),
                daemon=True
            )
            t.start()

    except KeyboardInterrupt:
        print(f"\n[{SERVER_NAME}] Shutting down server...")
    finally:
        try:
            server_socket.close()
        except Exception:
            pass


if __name__ == "__main__":
    port = 7778
    if len(sys.argv) > 1:
        raw_port = sys.argv[1].strip()
        try:
            port = int(raw_port)
        except ValueError:
            print(f"Invalid port '{raw_port}', falling back to default {port}")

    start_server(port=port)