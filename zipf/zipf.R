library(ggplot2)
library(sqldf)
library(gridExtra)
library(duckdb)
library(sads)
library(scales)
library(Cairo)
'%+%' <- function(x, y) paste0(x,y)

N = 10000000
calcCachedPercent = function(s, bufferSize) {
	x = 1:(N/(1/bufferSize))
	d = dzipf(x=x, N=N, s=s)
	return(sum(d))
}
ss = data.frame(ss = seq(0.1, 2, 0.1))
bb = data.frame(bb = c(1/4000, 1/2000, 1/1000))
sb = sqldf("select * from ss, bb")
head(sb)
sb$cached = apply(sb[,c('ss','bb')], 1, function(x) calcCachedPercent(x[1], x[2]))
ggplot(sb, aes(ss, cached*100, group=bb, color=factor(bb*100))) +
	geom_line() + 
	ylab("writes chached in write buffer [%]") +
	xlab("zipf skew factor") +
	ggtitle(paste0("Writes cached in buffer for buffer size: ")) +
	theme_bw()

?lapply

?quantile

N = 100
ss = seq(0, 2, 0.1)
s = 1

pzipf(c(1), N=N, s=s)

qzipf(c(0.04), N=N, s=s)


cpp = read.csv("zipfout.csv")
#rust = read.csv("rust-zipf/rzipfout.csv")

con <- dbConnect(duckdb(), dbdir = ":memory:")

dbExecute(con, "SELECT count(*) FROM read_csv('/Users/gabrielhaas/ssdiq/build/zipfout.csv');")

cpp = dbGetQuery(con, "SELECT *  
				 FROM read_csv('/Users/gabrielhaas/ssdiq/build/zipfoutN1.92TBintelSSDT0.8.csv")

tot = dbGetQuery(con, "SELECT count(*) as cnt
							FROM read_csv('/home/haas/ssdiq/build/fiotrace', names=['page'])")
tot

fiozipf = dbGetQuery(con, "with hist as (SELECT page, count(*) as cnt
							FROM read_csv('/home/haas/ssdiq/build/fiotrace', names=['page'])
							GROUP BY page
					), 
					tot as (select SUM(cnt) as cnt from hist)
					select 'fio' as type, page, cnt, cast(cnt as double) * 100.0 / (select cnt from tot) as percent
					from hist
					order by cnt desc
")
fiozipf$i = 0:(nrow(fiozipf)-1)

write.csv(fiozipf, "genzipfhist.csv", row.names = FALSE)

head(fiozipf)
tail(fiozipf)
head(max(fiozipf$page)) # 468843606

nrow(fiozipf)

hist = duckdf("select 'fio' as type, cast(i / (468843606/100) as long) as bucket, sum(cnt) as cnt, sum(percent) as percent
			from fiozipf 
			group by cast(i / (468843606/100) as long)
			order by bucket")

write.csv(hist, "fiozipf-intel-hist-100B.csv", row.names=FALSE)


sqldf("select sum(cnt), sum(pct) from fiozipf")

duckdf("select sum(percent) from fiozipf group by page")


cpp = dbGetQuery(con, "SELECT sum(percent) 
				 FROM read_csv('/Users/gabrielhaas/ssdiq/build/zipfout.csv')
				 where i < 10000 
				")

dbGetQuery(con, "SELECT count(*), sum(cnt), sum(percent), count_if(cnt == 0) as zero_cnt
				 FROM read_csv('/Users/gabrielhaas/ssdiq/data/zipfout.csv')
				")

fio = dbGetQuery(con, "SELECT * FROM read_csv('/Users/gabrielhaas/ssdiq/data/genzipfhist.csv')")

fio$i = 0:(nrow(fio)-1)

head(max(fio$i))

nrow(fio)
nrow(cpp)

head(cpp)
#head(rust)

sqldf("select count(*) from rust")

z = rbind(cpp, fio) #, rust)

head(z)
zf = sqldf("select * from z where i < 10000")
ggplot(zf, aes(i, percent, color=type)) +
	geom_line(alpha=0.25) +
	scale_y_log10() +
	theme_bw()



# from historgrams


fio = read.csv("fiozipf-intel-hist-100B.csv")
fio$type = 'fio'
cpp = read.csv("zipf-intel-t0p8-100buckets.csv")
head(cpp)
head(fio)

z = rbind(cpp, fio)
head(z)
zf = sqldf("select * from z")
zipfdist = ggplot(zf, aes(bucket, percent, color=type)) +
	geom_line() +
	scale_y_log10() +
	annotate("text", label = "rji", x=85, y=0.5, color="gray33") +
	annotate("text", label = "fio", x=70, y=0.1, color="gray33") +
	ylab("Access probability [%]") + 
	xlab("Distribution") + 
	theme_bw() +
	theme(legend.position = "none")
zipfdist

CairoPDF("paper-init-zipf-dist", width=2.2, height=2, bg = "transparent")
zipfdist
dev.off( )

