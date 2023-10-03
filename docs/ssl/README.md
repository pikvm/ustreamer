# Adding SSL
These days, browsers are not happy if you have HTTP content on an HTTPS page.
The browser will not show an HTTP stream on a page if the parent page is from a site which is using HTTPS.

The files in this folder configure an Nginx proxy in front of the µStreamer stream.
Using certbot, an SSL cert is created from Let's Encrypt and installed.
These scripts can be modified to add SSL to just about any HTTP server.

The scripts are not fire and forget.
They will require some pre-configuration and are interactive (you'll be asked questions while they're running).
They have been tested using the following setup.
1. A Raspberry Pi 4
1. µStreamer set up and running as a service
    1. Internally on port 8080
    1. Public port will be 5101
1. Verizon home Wi-Fi router
1. Domain registration from GoDaddy

## The Script
Below is an overview of the steps performed by `ssl-config.sh` (for Raspberry OS):
1. Install snapd - certbot uses this for installation
1. Install certbot
1. Get a free cert from Let's Encrypt using certbot
1. Install nginx
1. Configures nginx to proxy for µStreamer

## Steps
1. Create a public DNS entry.
    1. Pointing to the Pi itself or the public IP of the router behind which the Pi sits.
    1. This would be managed in the domain registrar, such as GoDaddy.
    1. Use a subdomain, such as `webcam.domain.com`
1. Port Forwarding
    1. If using a Wi-Fi router, create a port forwarding rule which passes traffic from port 80 to the Pi. This is needed for certbot to ensure your DNS entry reaches the Pi, even if your final port will be something else.
    1. Create a second rule for your final setup. For example, forward traffic from the router on port 5101 to the Pi's IP port 8080.
1. Update the ustreamer-proxy file in this folder
    1. Replace `your.domain.com` with a fully qualified domain, it's three places in the proxy file.
    1. Modify the line `listen 5101 ssl` port if needed. This is the public port, not the port on which the µStreamer service is running
    1. Modify `proxy_pass http://127.0.0.1:8080;` with the working address of the internal µStreamer service.
1. Run the script
    1. Stand buy, certbot asks some basic questions, such as email, domain, agree to terms, etc.
    1. `bash ssl-config.sh`
1. Test your URL!

## Down the Road
Two important points to keep in mind for the future:
1. Dynamic IP - Most routers do not have a static IP address on the WAN side. So, if you reboot your router or if your internet provider gives you a new IP, you'll have to update the DNS entry.
    1. Many routers have some sort of dynamic DNS feature. This would automatically update the DNS entry for you. That functionality is outside the scope of this document.
1. SSL Renewals - certbot automatically creates a task to renew the SSL cert before it expires. Assuming the Pi is running all the time, this shouldn't be an issue.

## Enjoy!
