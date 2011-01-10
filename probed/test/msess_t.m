%
% Plot result of msess storage test
%

a = csvread("m.txt");
s = size(a);

sess = unique(a(:,1));		% get session counts
probes = unique(a(:,2));	% get probe counts

z1 = zeros(length(sess), length(probes));
z2 = zeros(length(sess), length(probes));

for i=1:s(1)

	n_sess = a(i,1);
	n_probes = a(i,2);

	z1(find(sess == n_sess),find(probes == n_probes)) = n_sess*n_probes/a(i,3);
	z2(find(sess == n_sess),find(probes == n_probes)) = 3*n_sess*n_probes/a(i,4);

end

%figure(1);
title("Adding sessions");
mesh(sess, probes, z1);
xlabel("sessions");
ylabel("probes");
zlabel("operations per second")

figure(2);
mesh(z2);
title("Updating sessions");
mesh(sess, probes, z2);
xlabel("sessions");
ylabel("probes");
zlabel("operations per second")
