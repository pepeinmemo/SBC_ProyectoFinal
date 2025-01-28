import telebot
import requests

url = 'http://demo.thingsboard.io/api/v1/mXp38j5nRgARhj8N1r6R/telemetry'
bot = telebot.TeleBot('7908692887:AAEKu60ziq7XQNjc44CuBhDDNS70iiEe0i8')


@bot.message_handler(commands=['start'])
def start(message):
    bot.send_message(message.chat.id, "Bot preparado para funcionar", parse_mode='html')


@bot.message_handler(commands=['abrir'])
def abrir(message):
    data = {"AbiertoCerrado": 1}
    requests.post(url=url, json=data)
    bot.send_message(message.chat.id, "Abriendo...", parse_mode='html')


@bot.message_handler(commands=['cerrar'])
def cerrar(message):
    data = {"AbiertoCerrado": 0}
    requests.post(url=url, json=data)
    bot.send_message(message.chat.id, "Cerrando...", parse_mode='html')


@bot.message_handler(commands=['desbloquear'])
def desbloquear(message):
    data = {"EstadoBloqueo": 0}
    requests.post(url=url, json=data)
    bot.send_message(message.chat.id, "Desbloqueando...", parse_mode='html')


@bot.message_handler(commands=['bloquear'])
def bloquear(message):
    data = {"EstadoBloqueo": 1}
    requests.post(url=url, json=data)
    bot.send_message(message.chat.id, "Bloqueando...", parse_mode='html')


bot.infinity_polling()
