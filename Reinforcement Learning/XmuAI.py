'''
@Author: your name
@Date: 2020-07-21 08:16:52
@LastEditTime: 2020-07-27 14:12:23
@LastEditors: Please set LastEditors
@Description: In User Settings Edit
@FilePath: \CurlingAIPython\dist\XmuAI.py
'''
# -*- coding: utf-8 -*-
# import argparse
import socket
import tensorflow as tf
import tensorlayer as tl
import numpy as np
import time
import os
import sys

gpus = tf.config.experimental.list_physical_devices(device_type='GPU')
for gpu in gpus:
    tf.config.experimental.set_memory_growth(gpu, True)

#命令行参数输入
# parser = argparse.ArgumentParser(description= "Train or test neural net for XmuAI")
# parser.add_argument("--train", dest = "train", action= "store_true", default=True)
# parser.add_argument("--test", dest="test", action = "store_false")
# args = parser.parse_args()



#####################  static parameters  ###################
TEE_X = 2.375    # x of center of house
TEE_Y = 4.880    # y of center of house
HOUSE_R0 = 0.15
HOUSE_R1 = 0.61
HOUSE_R2 = 1.22
HOUSE_R = 1.870  # radius of house 
STONE_R = 0.145  # radius of stone 

PLAYAREA_X_MIN = 0.000 + STONE_R
PLAYAREA_X_MAX = 0.000 + 4.750 - STONE_R
PLAYAREA_Y_MIN = 2.650 + STONE_R
PLAYAREA_Y_MAX = 2.650 + 8.165 - STONE_R

X_SHOT_ERROR = 0.0242875
MIN_NUMBER = 1e-6
STONE_D = 2 * STONE_R

#####################  hyper parameters   ###################
train = True
RANDOMSEED = 1              # random seed

MAX_EPISODES = 2000         # total number of episodes for training
VAR = 30                     # control exploration

LR_A = 0.01                # learning rate for actor
LR_C = 0.02                # learning rate for critic
GAMMA = 0.9                 # reward discount
TAU = 0.01                  # soft replacement
MEMORY_CAPACITY = 10000      # size of replay buffer 
BATCH_SIZE = 150            # update batchsize

ACTOR_N_UNITS1 = 60
ACTOR_N_UNITS_LAST = 3
ACTOR_ACT_FUNC1 = tf.nn.relu
ACTOR_ACT_FUNC_LAST = tf.nn.tanh

CRITIC_N_UNITS1 = 60
CRITIC_N_UNITS_LAST = 1
CRITIC_ACT_FUNC1 = tf.nn.relu
CRITIC_ACT_FUNC_LAST = None

###########################  DDPG  ###########################
class DDPG(object):
    """
    DDPG class
    """

    def __init__(self, a_dim, s_dim, a_bound):
        # self.memory = np.zeros((MEMORY_CAPACITY, s_dim*2+a_dim+1), dtype = np.float32)
        self.memory = np.array([[]])
        self.pointer = 0
        self.a_dim, self.s_dim, self.a_bound = a_dim, s_dim, a_bound

        W_init = tf.random_normal_initializer(mean = 0, stddev= 0.3)
        b_init = tf.constant_initializer(0.1)

        def get_actor(input_state_shape, name = ''):
            inputs = tl.layers.Input(input_state_shape, name = "A_input")
            x = tl.layers.Dense(n_units = ACTOR_N_UNITS1, act = ACTOR_ACT_FUNC1, W_init = W_init, b_init = b_init, name = 'A_l1')(inputs)
            x = tl.layers.Dense(n_units = ACTOR_N_UNITS1, act = ACTOR_ACT_FUNC1, W_init = W_init, b_init = b_init, name = 'A_l2')(x)
            x = tl.layers.Dense(n_units = ACTOR_N_UNITS1, act = ACTOR_ACT_FUNC1, W_init = W_init, b_init = b_init, name = 'A_l3')(x)
            x = tl.layers.Dense(n_units=a_dim, act=ACTOR_ACT_FUNC_LAST, W_init=W_init, b_init=b_init,name="A_a")(x)
            x = tl.layers.Lambda(lambda x: np.array(a_bound) * x)(x)
            return tl.models.Model(inputs = inputs, outputs = x, name = "Actor" + name)
        
        def get_critic(input_state_shape, input_action_shape, name = ""):
            s = tl.layers.Input(input_state_shape, name = "C_s_input")
            a = tl.layers.Input(input_action_shape, name = "C_a_input")
            x = tl.layers.Concat(1)([s, a])
            x = tl.layers.Dense(n_units = CRITIC_N_UNITS1, act =CRITIC_ACT_FUNC1, W_init = W_init, b_init = b_init, name = "C_l1")(x)
            x = tl.layers.Dense(n_units = CRITIC_N_UNITS1, act =CRITIC_ACT_FUNC1, W_init = W_init, b_init = b_init, name = "C_l2")(x)
            x = tl.layers.Dense(n_units = CRITIC_N_UNITS1, act =CRITIC_ACT_FUNC1, W_init = W_init, b_init = b_init, name = "C_l3")(x)
            x = tl.layers.Dense(n_units = 1, W_init = W_init, b_init= b_init, name = "C_out")(x)
            return tl.models.Model(inputs = [s,a], outputs = x, name = "Critic" + name)
        
        self.actor = get_actor([None, s_dim])
        self.critic = get_critic([None, s_dim], [None, a_dim])
        self.actor.train()
        self.critic.train()
        
        def read_memory():
            def resource_path(relative_path):
                if getattr(sys, 'frozen', False): #是否Bundle Resource
                    base_path = sys._MEIPASS
                else:
                    base_path = os.path.abspath(".")
                return os.path.join(base_path, relative_path)
            file_path = resource_path('memory/memory.txt')
            return np.loadtxt(file_path)
        self.memory = read_memory()
        self.memory = self.memory.astype(np.float32)
        self.pointer = self.memory.shape[0]


        # 更新参数，只用于首次赋值
        def copy_para(from_model, to_model):
            for i,j in zip(from_model.trainable_weights, to_model.trainable_weights):
                j.assign(i)
        
        self.actor_target = get_actor([None, s_dim], name = '_target')
        copy_para(self.actor, self.actor_target)
        self.actor_target.eval()

        self.critic_target = get_critic([None, s_dim],[None, a_dim], name = '_target')
        copy_para(self.critic, self.critic_target)
        self.critic_target.eval()

        self.R = tl.layers.Input([None, 1], tf.float32, 'r')

        self.ema = tf.train.ExponentialMovingAverage(decay = 1 - TAU)
        
        self.actor_opt = tf.optimizers.Adam(LR_A)
        self.critic_opt = tf.optimizers.Adam(LR_C)
    
    def save_memory(self):
        def resource_path(relative_path):
            if getattr(sys, 'frozen', False): #是否Bundle Resource
                base_path = sys._MEIPASS
            else:
                base_path = os.path.abspath(".")
            return os.path.join(base_path, relative_path)
        file_path = resource_path('memory/memory.txt')
        self.memory = self.memory.astype(np.float32)
        np.savetxt(file_path, self.memory)
    def ema_update(self):
        paras = self.actor.trainable_weights + self.critic.trainable_weights
        self.ema.apply(paras)
        for i,j in zip(self.actor_target.trainable_weights+self.critic_target.trainable_weights, paras):
            i.assign(self.ema.average(j))

    def choose_action(self, s):
        return self.actor(np.array([s], dtype = np.float32))[0]
    
    def learn(self):
        # self.memory = self.memory.astype(np.float32)
        indices = np.random.choice(self.memory.shape[0], size = BATCH_SIZE)
        bt = self.memory[indices,:]
        bs = bt[:, :self.s_dim]
        ba = bt[:,self.s_dim:self.s_dim+self.a_dim]
        br = bt[:,-self.s_dim -1: -self.s_dim]
        bs_ = bt[:, -self.s_dim:]

        with tf.GradientTape() as tape:
            a_ = self.actor_target(bs_)
            q_ = self.critic_target([bs_, a_])
            y = br + GAMMA * q_
            q = self.critic([bs, ba])
            td_error = tf.losses.mean_squared_error(y,q)
        c_grads = tape.gradient(td_error, self.critic.trainable_weights)
        self.critic_opt.apply_gradients(zip(c_grads, self.critic.trainable_weights))
        
        with tf.GradientTape() as tape:
            a = self.actor(bs)
            q = self.critic([bs, a])
            a_loss = -tf.reduce_mean(q)
        a_grads = tape.gradient(a_loss, self.actor.trainable_weights)
        self.actor_opt.apply_gradients(zip(a_grads, self.actor.trainable_weights))
        self.ema_update()
    
    def store_transition(self, s, a, r, s_):
        s = s.astype(np.float32)
        s_ = s_.astype(np.float32)
        a = a.astype(np.float32)
        transition = np.hstack((s, a, [r], s_))
        index = self.pointer % MEMORY_CAPACITY
        if self.pointer < MEMORY_CAPACITY:
            if self.pointer == 0:
                transition = transition.astype(np.float32)
                self.memory = np.array([list(transition)])
            else:
                transition = transition.astype(np.float32)
                self.memory = np.append(self.memory,  [list(transition)], axis = 0)
        else:
            transition = transition.astype(np.float32)
            self.memory[index, :] = transition
        self.pointer += 1
        
    def save_ckpt(self):
        if not os.path.exists("model"):
            os.makedirs("model")
        tl.files.save_weights_to_hdf5('model/ddpg_actor.hdf5', self.actor)
        tl.files.save_weights_to_hdf5('model/ddpg_actor_target.hdf5', self.actor_target)
        tl.files.save_weights_to_hdf5('model/ddpg_critic.hdf5', self.critic)
        tl.files.save_weights_to_hdf5('model/ddpg_critic_target.hdf5', self.critic_target)
    
    def load_ckpt(self):
        def resource_path(relative_path):
            if getattr(sys, 'frozen', False): #是否Bundle Resource
                base_path = sys._MEIPASS
            else:
                base_path = os.path.abspath(".")
            return os.path.join(base_path, relative_path)
        tl.files.load_hdf5_to_weights_in_order(resource_path('model/ddpg_actor.hdf5'), self.actor)
        tl.files.load_hdf5_to_weights_in_order(resource_path('model/ddpg_actor_target.hdf5'), self.actor_target)
        tl.files.load_hdf5_to_weights_in_order(resource_path('model/ddpg_critic.hdf5'), self.critic)
        tl.files.load_hdf5_to_weights_in_order(resource_path('model/ddpg_critic_target.hdf5'), self.critic_target)
#初始化
shotnum = str("0")
state = []
shot = []
s =np.array([])
s_ =np.array([])
r = 0
a = np.array([])

# set random for reproduce
# np.random.seed(RANDOMSEED)
# tf.random.set_seed(RANDOMSEED)

a_dim = 3         # 动作维度为3 v h_x angle
s_dim = 33        # 32个球的位置坐标 1个shotnumber
a_bound = [2, (PLAYAREA_X_MAX - PLAYAREA_X_MIN)/2, 10]    # 速度2 -- 8， h_x -2.375 -- 2.375 angle -10 -- 10
ddpg = DDPG(a_dim, s_dim, a_bound)


def list_to_str(list):
    tmp = str(list)[1:-1].replace(',', '')
    res = "BESTSHOT " + tmp
    return res

#策略
def strategy():
    bestshot = ddpg.choose_action(state)
    bestshot = bestshot + [4.5, 0.0, 0.0]
    bestshot = np.clip(np.random.normal(bestshot, VAR),
     [2.5, PLAYAREA_X_MIN - TEE_X, -10], 
     [6.5, PLAYAREA_X_MAX - TEE_X,10])
    
    return bestshot

def get_dist(x, y):
    return np.sqrt((x - 2.375)**2 + (y - 4.88)**2)
    
def get_reward(s_):
    reward = 0
    s_ = [float(i) * 10.0 for i in s_]
    sorted_s = []
    i = 0
    while i < len(s_) - 1:
        sorted_s.append([get_dist(s_[i], s_[i+1]), i/2])
        i = i + 2
    sorted_s = sorted(sorted_s)
    i = 0
    if(sorted_s[i][0] < HOUSE_R + STONE_D):
        if(sorted_s[i][1] % 2 == order):
            while i < len(sorted_s):
                if(sorted_s[i][0] < HOUSE_R+ STONE_D and sorted_s[i][1]%2==order):
                    reward+=1
                    i += 1
                else:
                    break 
        else:
            while i < len(sorted_s):
                if(sorted_s[i][0] < HOUSE_R + STONE_D and sorted_s[i][1]%2!=order):
                    reward-=1
                    i+=1
                else:
                    break
    tmp_reward = 0
    for tmp in sorted_s:
        if tmp[1] % 2 == order:
            tmp_reward += -tmp[0]
    tmp_reward = tmp_reward / 10

    return (reward + tmp_reward)/10.0

if train == True:
    ddpg.load_ckpt()

#python 与客户端连接
host = '127.0.0.1'
port = 7788
obj = socket.socket()
connect_flag = False
while connect_flag == False:
    try:
        obj.connect((host, port)) 
        connect_flag = True
    except:
        connect_flag = False
ep_reward = 0
order = 1  #先后手
episode = 0
while True:
    ret = str(obj.recv(1024), encoding="utf-8")
    print("recv:" + ret)
    message = ret.split("\0")
    messageList = []
    for messageList in message:
        # messageList = ret.split(" ")
        messageList = messageList.split(" ")
        if messageList[0] == "NAME":
            if messageList[1] == str("Player1"):
                print("玩家1，首局先手")
            else:
                print("玩家2，首局后手")
        if messageList[0] == "ISREADY":
            time.sleep(0.5)
            obj.send(bytes("READYOK", encoding="utf-8"))
            print("send READYOK")
            obj.send(bytes("NAME XmuAI", encoding="utf-8"))
            print("send NAME XmuAI")
        
        if messageList[0] == "POSITION":
            if state:
                state = []
            # state.append(ret.split(" ")[1:31])
            state = messageList[1:33]
            state = [float(i) / 10.0 for i in state]
            
        if messageList[0] == "SETSTATE":
            shotnum = int(messageList[1])
            state.append(shotnum / 10.0)
            if ((shotnum >= 1 and order == 0) or (order == 1 and shotnum >= 2)) and shotnum % 2 != order:
                r = get_reward(state)
                ep_reward+=r
                s_ = np.array(state)
                a = np.array(shot)
                ddpg.store_transition(s,a,r,s_)
                if ddpg.pointer >= BATCH_SIZE:
                    ddpg.learn()
                if shotnum == 15 or shotnum == 16:
                    order = 1
                    ddpg.save_ckpt()

        if messageList[0] == "GO":
            if shotnum == 0:
                order = 0
            elif shotnum == 1:
                order = 1
            s = np.array(state)
            shot = strategy()  
            obj.send(bytes(list_to_str(shot), encoding="utf-8"))
            print(list_to_str(shot))
            episode+=1
            
        if messageList[0] == "MOTIONINFO":
            x_coordinate = float(messageList[1])
            y_coordinate = float(messageList[2])
            x_velocity = float(messageList[3])
            y_velocity = float(messageList[4])
            angular_velocity = float(messageList[5])
            # obj.send(bytes("SWEEP 0.0", encoding="utf-8"))
            print("SWEEP 0.0")

        if messageList[0] == "GAMEOVER":
            ddpg.save_memory()
            break
    if messageList[0] == "GAMEOVER":
            break
