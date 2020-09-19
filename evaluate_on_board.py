from math import ceil
from statistics import median, stdev, mean, quantiles
from subprocess import run
from time import sleep

from paramiko import SSHClient, AutoAddPolicy
from scp import SCPClient


# write computed metrics to evaluation file in csv style
def write_statistics(output, datasets, sep=','):
    output.write(sep.join(['name', 'min', 'max', 'range', 'median', 'iqr', 'mean', 'stdev']) + '\n')
    for name, lst in datasets:
        output.write(sep.join([f'{name}',
                               f'{min(lst):.3f}',
                               f'{max(lst):.3f}',
                               f'{max(lst) - min(lst):.3f}',
                               f'{median(lst):.3f}',
                               f'{quantiles(lst)[2] - quantiles(lst)[0]:.3f}',
                               f'{mean(lst):.3f}',
                               f'{stdev(lst):.3f}']) + '\n')


# general configuration constants
OPC_UA_SERVER_START_PORT = 4840
PUBSUB_URL = 'opc.udp://224.0.0.1:15000/'
EVALUATION_FILE_NAME = './logs/evaluation.csv'
VLAN_ID = 1
TIMEOUT_QBV_START_SECONDS = 2
# time interval from interrupt to return of publish callback
MAX_PUBLISH_DELAY_NS = 300_000
QBV_CHUNK_SIZE_NS = 500_000
CYCLE_TIME_NS = 1_000_000
MEASUREMENTS = 10_000

assert 1_000_000_000 % CYCLE_TIME_NS == 0
assert CYCLE_TIME_NS > MAX_PUBLISH_DELAY_NS

# configure the publisher board
PUB_SSH_IP = '192.168.0.1'
PUB_VLAN_IP = '192.168.0.2'
PUB_SSH_USERNAME = 'root'
PUB_SSH_PASSWORD = ''
PUB_INTERFACE = 'sw0p5'

# configure the subscriber board
SUB_SSH_IP = '192.168.0.1'
SUB_VLAN_IP = '192.168.0.3'
SUB_SSH_USERNAME = 'root'
SUB_SSH_PASSWORD = ''
SUB_INTERFACE = 'sw0p5'

# build whole project to work with the newest executables
run('build.sh', shell=True, check=True, capture_output=True)

# connect to publisher board
pub_ssh_client = SSHClient()
pub_ssh_client.set_missing_host_key_policy(AutoAddPolicy())
pub_ssh_client.connect(PUB_SSH_IP, 22, PUB_SSH_USERNAME, PUB_SSH_PASSWORD)
pub_scp_client = SCPClient(pub_ssh_client.get_transport())

# connect to subscriber board
sub_ssh_client = SSHClient()
sub_ssh_client.set_missing_host_key_policy(AutoAddPolicy())
sub_ssh_client.connect(SUB_SSH_IP, 22, SUB_SSH_USERNAME, SUB_SSH_PASSWORD)
sub_scp_client = SCPClient(sub_ssh_client.get_transport())

# setup VLAN for selected publisher and subscriber interface
for ssh_client, interface, vlan_ip, ssh_username in \
        zip([pub_ssh_client, sub_ssh_client], [PUB_INTERFACE, SUB_INTERFACE],
            [PUB_VLAN_IP, SUB_VLAN_IP], [SUB_SSH_USERNAME, PUB_SSH_USERNAME]):
    ssh_client.exec_command(f'rm -rf /home/{ssh_username}/*')[1].read()
    ssh_client.exec_command(f'bridge vlan add vid {VLAN_ID} dev {interface}')[1].read()
    ssh_client.exec_command(f'ip link add link {interface} name {interface}.{VLAN_ID} type vlan id {VLAN_ID} '
                            f'egress-qos-map  0:0 1:1 2:2 3:3 4:4 5:5 6:6 7:7 '
                            f'ingress-qos-map 0:0 1:1 2:2 3:3 4:4 5:5 6:6 7:7')[1].read()
    ssh_client.exec_command(f'ip addr add {vlan_ip}/24 dev {interface}.{VLAN_ID}')[1].read()
    ssh_client.exec_command(f'ip link set {interface}.{VLAN_ID} up')[1].read()

# configure QBV for the publisher
remaining_cycle_time = CYCLE_TIME_NS - MAX_PUBLISH_DELAY_NS
full_chunk_sizes = remaining_cycle_time // QBV_CHUNK_SIZE_NS
remaining_chunk_size = remaining_cycle_time % QBV_CHUNK_SIZE_NS
qbv_config_values = [('sgs', str(MAX_PUBLISH_DELAY_NS), '0x00')] + [('sgs', str(QBV_CHUNK_SIZE_NS), '0xff')] * full_chunk_sizes + \
                    [('sgs', str(remaining_chunk_size), '0xff')] if remaining_chunk_size > 0 else []
qbv_config = '\n'.join([' '.join(x) for x in qbv_config_values])
pub_ssh_client.exec_command(f'echo "{qbv_config}" > /home/{PUB_SSH_USERNAME}/qbv.cfg')[1].read()
pub_ssh_client.exec_command(f'tsntool st wrcl {PUB_INTERFACE} /home/{PUB_SSH_USERNAME}/qbv.cfg')[1].read()
start_time = ceil(float(pub_ssh_client.exec_command(f'cat /sys/class/net/{PUB_INTERFACE}/ieee8021ST/CurrentTime')[1].read())) + TIMEOUT_QBV_START_SECONDS
pub_ssh_client.exec_command(f'tsntool st configure {start_time}.0 1/{1_000_000_000 // CYCLE_TIME_NS} 0 {PUB_INTERFACE}')[1].read()
sleep(2 * TIMEOUT_QBV_START_SECONDS)

# start both executables and wait for termination
outputs = []
for index, (exec_type, ssh_client, scp_client, ssh_username, vlan_ip) \
        in enumerate(zip(['subscriber', 'publisher'], [sub_ssh_client, pub_ssh_client],
                         [sub_scp_client, pub_scp_client], [SUB_SSH_USERNAME, PUB_SSH_USERNAME],
                         [SUB_VLAN_IP, PUB_VLAN_IP])):
    scp_client.put(f'./build/opcua_{exec_type}_arm', f'/home/{ssh_username}/opcua_{exec_type}_arm')
    ssh_client.exec_command(f'chmod +x /home/{ssh_username}/opcua_{exec_type}_arm')[1].read()
    _, stdout, _ = ssh_client.exec_command(f'/home/{ssh_username}/opcua_{exec_type}_arm '
                                           f'{vlan_ip} {PUBSUB_URL} {OPC_UA_SERVER_START_PORT + index} {CYCLE_TIME_NS} {MEASUREMENTS}')
    outputs.append(stdout)
for out in outputs:
    out.read()

# reset QBV for the publisher
pub_ssh_client.exec_command(f'tsntool st reset {PUB_INTERFACE}')[1].read()

# reset VLAN config for selected publisher and subscriber interface
for ssh_client, interface, vlan_ip in zip([pub_ssh_client, sub_ssh_client], [PUB_INTERFACE, SUB_INTERFACE], [PUB_VLAN_IP, SUB_VLAN_IP]):
    ssh_client.exec_command(f'ip link delete {interface}.{VLAN_ID}')[1].read()
    ssh_client.exec_command(f'bridge vlan del vid {VLAN_ID} dev {interface}')[1].read()

# collect produced log files during execution
pub_scp_client.get(f'/home/{PUB_SSH_USERNAME}/publish.csv', './logs/publish.csv')
sub_scp_client.get(f'/home/{SUB_SSH_USERNAME}/subscribe.csv', './logs/subscribe.csv')

# read data from log files into memory
with open('./logs/publish.csv') as publishFile:
    publishData = [[int(x.split(',')[0]), int(x.split(',')[1])] for x in publishFile]
with open('./logs/subscribe.csv') as subscribeFile:
    subscribeData = [[int(x.split(',')[0]), int(x.split(',')[1])] for x in subscribeFile]

assert [x[0] for x in publishData] == [x[0] for x in subscribeData] and len(publishData) == MEASUREMENTS

# compute the most important metrics
publish_intervals = [(after[1] - before[1]) * 1E-6 for before, after in zip(publishData, publishData[1:])]
subscribe_intervals = [(after[1] - before[1]) * 1E-6 for before, after in zip(subscribeData, subscribeData[1:])]
transmission_times = [(end[1] - start[1]) * 1E-6 for start, end in zip(publishData, subscribeData)]
target_publish_times = [publishData[0][1] + x * CYCLE_TIME_NS for x in range(len(publishData))]
publish_jitters = [(actual[1] - target) * 1E-6 for actual, target in zip(publishData, target_publish_times)]
target_subscribe_times = [subscribeData[0][1] + x * CYCLE_TIME_NS for x in range(len(subscribeData))]
subscribe_jitters = [(actual[1] - target) * 1E-6 for actual, target in zip(subscribeData, target_subscribe_times)]

# write metrics to evaluation file
with open(EVALUATION_FILE_NAME, 'w') as file:
    write_statistics(file,
                     [('publish interval [ms]', publish_intervals),
                      ('subscribe interval [ms]', subscribe_intervals),
                      ('transmission time [ms]', transmission_times),
                      ('publish jitter [ms]', publish_jitters),
                      ('subscribe jitter [ms]', subscribe_jitters)])

# print evaluation file
with open(EVALUATION_FILE_NAME) as file:
    print(file.read())
