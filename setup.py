# setup.py

from setuptools import setup, Extension
import pybind11

# Определяем наш C++ модуль как "расширение" (Extension) для Python
ext_modules = [
    Extension(
        'kestog_core',  # Имя модуля, которое будет использоваться в Python (import kestog_core)
        [
            'KestoG_Core.cpp',  # Исходный файл с игровой логикой
            'bindings.cpp'      # Исходный файл с "мостом" pybind11
        ],
        # Указываем, где искать заголовочные файлы.
        # pybind11.get_include() автоматически находит путь к заголовкам pybind11.
        include_dirs=[
            pybind11.get_include()
        ],
        # Явно указываем, что это C++ код
        language='c++',
        # Дополнительные флаги для компилятора
        extra_compile_args=[
            '-std=c++17',       # Использовать стандарт C++17
            '-O3',              # Максимальный уровень оптимизации компилятора
            '-shared',          # Создать разделяемую библиотеку (.so)
            '-fPIC',            # Position-Independent Code (обязательно для разделяемых библиотек)
            '-Wall',            # Включить основные предупреждения компилятора
            '-Wextra'           # Включить дополнительные предупреждения
        ]
    )
]

# Запускаем процесс сборки
setup(
    name='kestog_core',
    version='1.1', # Версия вашего модуля
    author='AI Guru & Team', # Автор
    description='High-performance giveaway checkers core module',
    ext_modules=ext_modules, # Указываем список расширений для сборки
)
