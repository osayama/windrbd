sudo iptables -I INPUT 1 -p tcp --destination-port 7684 -j DROP
sudo iptables -I INPUT 1 -p tcp --source-port 7684 -j DROP
sudo iptables -I OUTPUT 1 -p tcp --destination-port 7684 -j DROP
sudo iptables -I OUTPUT 1 -p tcp --source-port 7684 -j DROP
