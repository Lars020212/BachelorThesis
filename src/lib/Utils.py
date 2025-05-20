import os
import shutil

gettysburg_address = """
Four score and seven years ago our fathers brought forth on this continent, a
new nation, conceived in Liberty, and dedicated to the proposition that all men
are created equal.
Now we are engaged in a great civil war, testing whether that nation, or any
nation so conceived and so dedicated, can long endure. We are met on a great
battle-field of that war. We have come to dedicate a portion of that field, as a
final resting place for those who here gave their lives that that nation might
live. It is altogether fitting and proper that we should do this.
But, in a larger sense, we can not dedicate -- we can not consecrate -- we can
not hallow -- this ground. The brave men, living and dead, who struggled here,
have consecrated it, far above our poor power to add or detract. The world will
little note, nor long remember what we say here, but it can never forget what
they did here. It is for us the living, rather, to be dedicated here to the
unfinished work which they who fought here have thus far so nobly advanced. It
is rather for us to be here dedicated to the great task remaining before us --
that from these honored dead we take increased devotion to that cause for which
they gave the last full measure of devotion -- that we here highly resolve that
these dead shall not have died in vain -- that this nation, under God, shall
have a new birth of freedom -- and that government of the people, by the people,
for the people, shall not perish from the earth.
Abraham Lincoln, November 19, 1863
"""

import re
import random
import pandas as pd
import os
#from ironxl import *



class Utils:
    def __init__(self):
        pass

    def get_all_file_paths(self, directory):
        file_paths = []
        for filename in os.listdir(directory):
            if filename not in ['.', '..']:
                file_path = os.path.join(directory, filename)
                file_paths.append(file_path)
        return file_paths

    def remove_all_files(self, directory):
        for filename in os.listdir(directory):
            if filename not in ['.', '..']:
                file_path = os.path.join(directory, filename)
                if os.path.isfile(file_path) or os.path.islink(file_path):
                    os.unlink(file_path)
                elif os.path.isdir(file_path):
                    shutil.rmtree(file_path)

    def create_dynamic_file(self, total_size, in_path, out_path, specific_filename=None):
        files = os.listdir(in_path)
        if specific_filename:
            files = [specific_filename]

        file_size = total_size // len(files)

        for file in files:
            with open(os.path.join(in_path, file), 'rb') as f:
                content = f.read()

            new_filename = os.path.join(out_path, file)
            new_content = (content * (file_size // len(content) + 1))[:file_size]

            with open(new_filename, 'wb') as new_file:
                new_file.write(new_content)

            print(f"Created file {new_filename} of size: {len(new_content) // 1000} Kb")

    #https://gist.github.com/jeffbass/4cde8fa8abf3b1dcb2d7c56391ee951e
    def create_file(self, size, path):


        build_text = ""
        with open("/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/payloads/silesia/webster", "r") as f:
            build_text = f.read()

        if len(build_text) < size:
            raise Exception(f"size {size} is larger than the provided file size of {len(build_text)}")

        filename = path + '/input' + '.txt'

        content = build_text[:size]
        with open(filename, 'w') as text_file:
            text_file.write(content)
        print(f"Created file of size: {len(content) // 1000} Kb")


    def clear_file(self, path):
        filename = path + '/input' + '.txt'
        with open(filename, "w"):
            pass

    def get_data_buffer(self, files_directory):
        files = os.listdir(files_directory)
        buffer = ""
        for file_name in files:
            file_path = os.path.join(files_directory, file_name)
            with open(file_path, "rb") as file:
                while True:
                    chunk = file.read(4096)
                    if not chunk:
                        break
                    buffer += chunk.decode('utf-8')  # Assuming decoding to UTF-8
        #print("Created buffer of size " + str(len(buffer) / 1000) + " kb")
        return buffer

    def chunk_string(self, buffer: str, chunk_size: int):
        return [buffer[i:i + chunk_size] for i in range(0, len(buffer), chunk_size)]

    def buffer_to_byte_array(self, buffer):
        byte_array = buffer.encode('utf-8')  # Encoding the string to UTF-8 byte array
        return byte_array


    def get_data_buffer_compression(self):
        pass
