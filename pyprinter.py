from escpos import printer
import matplotlib.pyplot as plt
p = printer.File("/dev/usb/lp3")
p.image('div-como-imagem.png')