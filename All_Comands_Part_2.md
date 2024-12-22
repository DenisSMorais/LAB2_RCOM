TASK 1:

TUX3:
```bash
ifconfig eth1 172.16.50.1/24
ifconfig eth1
```
TUX4:

```bash
ifconfig eth1 172.16.50.254/24
ifconfig eth1
```
TUX3:
```bash
arp -a
arp -d 10.227.20.3
...
(arp -d all ips)

ping 172.16.50.254 (recebido)
```


-TASK 2:

TUX2:
```bash
ifconfig eth1 172.16.51.1/24
ifconfig eth1
```

TUX3:
Open GTKTERM (switch)
```bash
/interface bridge add name=bridge50
/interface bridge add name=bridge51

/interface bridge port remove [find interface=ether1]
/interface bridge port remove [find interface=ether2]
/interface bridge port remove [find interface=ether9]
/interface bridge port remove [find interface=ether10]
/interface bridge port remove [find interface=ether11]

/interface bridge port add bridge=bridge50 interface=ether1
/interface bridge port add bridge=bridge50 interface=ether2
/interface bridge port add bridge=bridge51 interface=ether9
/interface bridge port add bridge=bridge51 interface=ether10
/interface bridge port add bridge=bridge51 interface=ether11
```


-TASK 3:

TUX4:
```bash
ifconfig eth2 172.16.51.253/24
ifconfig eth2

ip eth2 tux4 = 172.16.51.253

sysctl net.ipv4.ip_forward=1
sysctl net.ipv4.icmp_echo_ignore_broadcasts=0

ifconfig eth1
ifconfic eth2
```
TUX3:
```bash
route add -net 172.16.51.0/24 gw 172.16.50.254
```
TUX2:
```bash
route add -net 172.16.50.0/24 gw 172.16.51.253
```
TUX3:
```bash
ping 172.16.50.254
ping 172.16.51.253
ping 172.16.51.1
```
Clean the arp table:
```bash
arp -a
arp -d (all ips)
```


-Task 4:

Open GTKTERM (router):
```bash
/system reset-configuration

/ip address add address=172.16.1.59/24 interface=ether1
/ip address add address=172.16.51.254/24 interface=ether2
```

TUX4:
```bash
route add -net 172.16.1.0/24 gw 172.16.51.254
```
TUX2:
```bash
route add -net 172.16.1.0/24 gw 172.16.51.254
```
TUX3:
```bash
route add -net 172.16.1.0/24 gw 172.16.50.254
```
ROUTE:
```bash
/ip route add dst-address=172.16.50.0/24 gateway=172.16.51.253
/ip route add dst-address=0.0.0.0/0 gateway=172.16.1.254
```
TUX3:
```bash
ping 172.16.51.253
ping 172.16.51.254
ping 172.16.50.254
```

TUX2:
```bash
sysctl net.ipv4.conf.eth1.accept_redirects=0
sysctl net.ipv4.conf.all.accept_redirects=0
```
TUX3:
```bash
ping 172.16.1.254 (FTP server)
```

ROUTER:
```bash
/ip firewall nat disable 0
```

TUX3:
```bash
ping 172.16.1.254
```

-TASK 5:

TUX3, TUX4 and Tux2:
```bash
sudo /etc/resolv.conf 
```
Write in the resolv.conf archive:
```bash
server services.netlab.fe.up.pt with IP 10.227.20.3
```
Ping in the Ips:
```bash
ping google.com 
ping netlab.fe.up.pt. 
```
