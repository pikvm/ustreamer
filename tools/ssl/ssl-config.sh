#!/bin/sh

echo -e "\e[32mInstalling snapd...\e[0m"
sudo apt install snapd -y
sudo snap install core


echo -e "\e[32mInstalling certbot, don't leave, it's going to ask questions...\e[0m"
sudo snap install --classic certbot
sudo ln -s /snap/bin/certbot /usr/bin/certbot
sudo certbot certonly --standalone
sudo certbot renew --dry-run


echo -e "\e[32mInstalling nginx...\e[0m"
sudo apt-get install nginx -y
sudo cp ustreamer-proxy /etc/nginx/sites-available/ustreamer-proxy
sudo ln -s /etc/nginx/sites-available/ustreamer-proxy /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl reload nginx
