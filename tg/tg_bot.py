import telebot
import config_manager
import bot_service
import util
import mqtt


env, devices, db = config_manager.load_config()

bot = telebot.TeleBot(env["ir_bot_token"])

mqttClients = mqtt.MutiMqttClients(devices, bot)

service = bot_service.Service(bot, env, devices, db)

def Permissions(func):
    def wrapper(*args, **kwargs):
        # 只保留ascii可见字符和空格换行
        args[0].text =  ''.join(filter(lambda char: 32 <= ord(char) <= 126 or char == '\n', args[0].text))
        print(args[0])
        message = args[0]
        if message.chat.id not in db["user"]:
            bot.reply_to(message, f"authentication required")
            return 
        data = func(*args, **kwargs)  # 调用被装饰的函数，并传递所有参数
        if data:
            mqttClients.get(db["device"]["name"]).publish(db["device"]["ir_pub_topic"], str(data), 0)
            bot.reply_to(message, f"{str(data)} is transmitted")
    return wrapper


@bot.message_handler(commands=['preference'])
@Permissions
def bot_preference(message):
    lines = [i for i in message.text.split('\n') if i]
    # 添加删除别名
    adding = ""
    for line in lines[1:]: 
        line = line.strip()
        if line[0] == "+": #不存在空行
            adding = line[1:]
            db["preference"][adding] = []
            continue
        if line[0] == "-":
            adding = ""
            if line[1:] in db["preference"]: del db["preference"][line[1:]]
            continue
        if adding:
            db["preference"][adding].append(line)
    util.save_dict(util.DBFILE, db)

    # 返回别名列表
    preference_msg = "\n".join([ f"{k}\n    {"\n    ".join(v)}" for k,v in db["preference"].items()])
    bot.reply_to(message, f"alias list:\n{preference_msg}")

    # 执行别名
    args = [i for i in lines[0].split(" ")[1:] if i]
    for alias in args:
        bot.send_message(message.chat.id, f"executing {alias} ...")
        if alias in db["preference"]:
            for seq in db["preference"][alias]:
                cmd = [i for i in seq.split(" ") if i]
                if len(cmd) == 0: continue
                cmd = cmd[0]
                message.text = seq
                try:
                    data = util.dynamic_call(service, cmd, message)
                    if data:
                        mqttClients.get(db["device"]["name"]).publish(db["device"]["ir_pub_topic"], str(data), 0)
                        bot.send_message(message.chat.id, f"{str(data)} is transmitted")
                except AttributeError as e:
                    bot.send_message(message.chat.id, f"command {cmd} not found")

@bot.message_handler(commands=['copy'])
@Permissions
def bot_copy(message):
    return service.copy(message)
    

@bot.message_handler(commands=['exec'])
@Permissions
def bot_exec(message):
    return service.exec(message)
    
@bot.message_handler(commands=['terminate'])
@Permissions
def bot_terminate(message):
    return service.terminate(message)


@bot.message_handler(commands=['cmdlist'])
@Permissions
def bot_cmdlist(message):
    return service.cmdlist(message)


@bot.message_handler(commands=['taskidlist'])
@Permissions
def bot_taskidlist(message):
    return service.taskidlist(message)


@bot.message_handler(commands=['tasklist'])
@Permissions
def bot_tasklist(message):
    return service.tasklist(message)

@bot.message_handler(commands=['task'])
@Permissions
def bot_task(message):
   return service.task(message)

@bot.message_handler(commands=['device'])
@Permissions
def bot_device(message):
    service.device(message) 

@bot.message_handler(commands=['usermod'])
@Permissions
def bot_usermod(message):
    service.usermod(message)


@bot.message_handler(commands=['auth'])
def bot_request(message):
    service.auth(message)


@bot.message_handler(commands=['start'])
def bot_start(message):
    service.start(message)

@bot.message_handler(commands=['help'])
def bot_help(message):
    service.help(message)
    

bot.infinity_polling()



