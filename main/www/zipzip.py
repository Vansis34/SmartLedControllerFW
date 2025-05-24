import gzip
import shutil

def compress_file_gz(input_file, output_file):
    with open(input_file, 'rb') as f_in:
        with gzip.open(output_file, 'wb') as f_out:
            shutil.copyfileobj(f_in, f_out)

# Пример использования
input_file = 'main/www/index.html'  # Исходный файл
output_file = 'main/www/index.html.gz'  # Сжатый файл
compress_file_gz(input_file, output_file)
print(f"Файл {input_file} сжат в {output_file}")