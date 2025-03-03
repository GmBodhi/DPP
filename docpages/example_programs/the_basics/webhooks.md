\page webhooks Webhooks

Webhooks are a simple way to post messages from other apps and websites into Discord.
They allow getting automated messages and data updates sent to a text channel in your server. [Read more](https://support.discord.com/hc/en-us/articles/228383668) in this article about Webhooks.

The following code shows how to send messages in a channel using a webhook.

\include{cpp} webhooks.cpp

\note For just sending a webhook, the example above is overkill. If you are here because you searched for 'how to send a Discord webhook in C++', you'll quickly learn that D++ can do much more than just send webhooks! The
above is just a very simple example. You can also send embed messages. All you have to do is to add an embed to the message you want to send. If you want to, you can also send it into a thread. For further examples, check
the rest of the site.
