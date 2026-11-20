// Package guild owns guild creation, membership roles, and guild online presence.
//
// Exclusive PostgreSQL ownership: guilds, guild_members.
// Redis keys / NATS: guild:online:*, IU_GUILD_STREAM (iu.guild.>).
package guild
