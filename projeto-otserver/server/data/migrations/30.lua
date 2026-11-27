function onUpdateDatabase()
	print("> Updating database to version 31 (vocation collection)")
	db.query([[
		CREATE TABLE IF NOT EXISTS `player_vocation_collection` (
			`player_id` int NOT NULL,
			`base_vocation` smallint unsigned NOT NULL,
			`fragments` int unsigned NOT NULL DEFAULT '0',
			`unlocked` tinyint NOT NULL DEFAULT '0',
			`has_profile` tinyint NOT NULL DEFAULT '0',
			`vocation` smallint unsigned NOT NULL DEFAULT '0',
			`level` int unsigned NOT NULL DEFAULT '1',
			`experience` bigint unsigned NOT NULL DEFAULT '0',
			`health` int NOT NULL DEFAULT '150',
			`healthmax` int NOT NULL DEFAULT '150',
			`mana` int NOT NULL DEFAULT '0',
			`manamax` int NOT NULL DEFAULT '0',
			`maglevel` int unsigned NOT NULL DEFAULT '0',
			`manaspent` bigint unsigned NOT NULL DEFAULT '0',
			`cap` int unsigned NOT NULL DEFAULT '400',
			`soul` int unsigned NOT NULL DEFAULT '0',
			`skill_fist` int unsigned NOT NULL DEFAULT '10',
			`skill_fist_tries` bigint unsigned NOT NULL DEFAULT '0',
			`skill_club` int unsigned NOT NULL DEFAULT '10',
			`skill_club_tries` bigint unsigned NOT NULL DEFAULT '0',
			`skill_sword` int unsigned NOT NULL DEFAULT '10',
			`skill_sword_tries` bigint unsigned NOT NULL DEFAULT '0',
			`skill_axe` int unsigned NOT NULL DEFAULT '10',
			`skill_axe_tries` bigint unsigned NOT NULL DEFAULT '0',
			`skill_dist` int unsigned NOT NULL DEFAULT '10',
			`skill_dist_tries` bigint unsigned NOT NULL DEFAULT '0',
			`skill_shielding` int unsigned NOT NULL DEFAULT '10',
			`skill_shielding_tries` bigint unsigned NOT NULL DEFAULT '0',
			`skill_fishing` int unsigned NOT NULL DEFAULT '10',
			`skill_fishing_tries` bigint unsigned NOT NULL DEFAULT '0',
			PRIMARY KEY (`player_id`, `base_vocation`),
			FOREIGN KEY (`player_id`) REFERENCES `players` (`id`) ON DELETE CASCADE
		) ENGINE=InnoDB DEFAULT CHARACTER SET=utf8
	]])
	return true
end
